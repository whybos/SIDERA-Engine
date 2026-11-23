#include "assets/AssetDatabase.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace sidera;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { ++g_passed; } \
        else { ++g_failed; std::cerr << "[FAIL] " << msg << "\n"; } \
    } while (0)

static void testGUIDAndRelocation() {
    const AssetGUID g1 = AssetGUID::generate();
    const AssetGUID g2 = AssetGUID::generate();
    CHECK(g1.isValid() && g2.isValid() && g1 != g2, "Generated GUIDs are valid and unique");
    CHECK(AssetGUID::fromString(g1.toString()) == g1, "GUID hex round-trip matches");
    CHECK(!AssetGUID::fromString("invalid").isValid(), "Invalid GUID string rejected");

    AssetDatabase db;
    AssetMetadata m{};
    m.guid = g1;
    m.type = AssetType::Material;
    m.relativeSourcePath = "materials/rock.smat";
    db.registerAsset(m);
    CHECK(db.findByPath("materials/rock.smat") != nullptr, "Lookup by relative path succeeds");
    CHECK(db.relocateAsset(g1, "materials/env/rock.smat"), "Relocate preserves GUID");
    CHECK(db.findByPath("materials/rock.smat") == nullptr, "Old path removed after relocation");
    CHECK(db.findByPath("materials/env/rock.smat")->guid == g1, "New path resolves to same GUID");
}

static void testImportersIncrementalAndCooking() {
    const std::filesystem::path tmpRoot = "asset_pipeline_unit_test_tmp";
    const std::filesystem::path cookedDir = tmpRoot / "cooked";
    std::error_code ec;
    std::filesystem::remove_all(tmpRoot, ec);
    std::filesystem::create_directories(tmpRoot / "src", ec);

    // 1. Write real OBJ fixture
    {
        std::ofstream f(tmpRoot / "src/tri.obj");
        f << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    // 2. Write real PPM texture fixture
    {
        std::ofstream f(tmpRoot / "src/normal.ppm");
        f << "P3\n2 2\n255\n128 128 255 128 128 255 128 128 255 128 128 255\n";
    }
    // 3. Write real 44-byte RIFF/WAVE fixture (44100 Hz, 2ch, 16-bit)
    {
        uint8_t wav[44]{};
        wav[0]='R'; wav[1]='I'; wav[2]='F'; wav[3]='F';
        wav[8]='W'; wav[9]='A'; wav[10]='V'; wav[11]='E';
        wav[22]=2; wav[23]=0; // 2 channels
        wav[24]=0x44; wav[25]=0xAC; wav[26]=0; wav[27]=0; // 44100 Hz
        wav[34]=16; wav[35]=0; // 16-bit
        std::ofstream f(tmpRoot / "src/sfx.wav", std::ios::binary);
        f.write(reinterpret_cast<const char*>(wav), 44);
    }
    // 4. Write real GLSL shader fixture
    {
        std::ofstream f(tmpRoot / "src/test.comp");
        f << "#version 450\nvoid main() {}\n";
    }

    AssetDatabase db;
    db.setProjectRoot(tmpRoot);
    db.registerDefaultImporters();

    const AssetGUID meshId = db.importFile("src/tri.obj", cookedDir);
    AssetImportSettings normSettings{};
    normSettings.texturePreset = TextureImportPreset::NormalLinear;
    const AssetGUID texId = db.importFile("src/normal.ppm", cookedDir, normSettings);
    const AssetGUID wavId = db.importFile("src/sfx.wav", cookedDir);
    const AssetGUID shdId = db.importFile("src/test.comp", cookedDir);

    CHECK(db.findByGUID(meshId)->status == AssetImportStatus::Imported, "OBJ mesh imported");
    CHECK(db.findByGUID(texId)->status == AssetImportStatus::Imported && !isSRGBTexturePreset(normSettings.texturePreset),
          "Normal texture imported as Linear (not sRGB)");
    CHECK(db.findByGUID(wavId)->status == AssetImportStatus::Imported, "WAV audio imported");
    CHECK(db.findByGUID(shdId)->status == AssetImportStatus::Imported, "GLSL shader imported");

    // Dependency graph & reverse lookup
    CHECK(db.addDependency(meshId, texId), "Mesh -> Texture dependency added");
    CHECK(db.getReverseDependents(texId).size() == 1u && db.getReverseDependents(texId)[0] == meshId,
          "Reverse dependency lookup finds Mesh");
    CHECK(!db.hasCircularDependency(meshId), "No circular dependency");

    // Incremental check: unchanged assets should not need reimport
    CHECK(!db.needsReimport(meshId), "Unchanged OBJ does not need reimport");
    const AssetCookReport cook1 = db.cookAll(cookedDir);
    CHECK(cook1.skippedIncremental == 4u && cook1.failedAssets == 0u, "Incremental cook skips all 4 unchanged assets");
    CHECK(std::filesystem::exists(cookedDir / "asset_manifest.json"), "asset_manifest.json generated");

    // Modify source file -> triggers reimport detection
    {
        std::ofstream f(tmpRoot / "src/tri.obj", std::ios::app);
        f << "v 1 1 0\n";
    }
    CHECK(db.needsReimport(meshId), "Modified OBJ triggers needsReimport");

    std::filesystem::remove_all(tmpRoot, ec);
}

int main() {
    testGUIDAndRelocation();
    testImportersIncrementalAndCooking();

    std::cout << "test_asset_pipeline: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
