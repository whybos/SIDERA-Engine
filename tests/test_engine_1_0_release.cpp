#include "core/Version.h"
#include "editor/EditorApplication.h"
#include "runtime/FormatMigration.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace sidera;
using namespace sidera::runtime;
using namespace sidera::editor;

namespace {

int g_failures = 0;

void check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    } else {
        std::cout << "[PASS] " << msg << "\n";
    }
}

void testEngineVersion() {
    std::cout << "\n--- 1. Engine 1.0 Version Identity ---\n";
    const auto v = getEngineVersion();
    check(v.major == 1 && v.minor == 0 && v.patch == 0, "Semantic version is 1.0.0");
    check(v.prerelease == "rc1", "Prerelease tag is rc1");
    check(kSideraVersionString == "1.0.0-rc1", "Version string is 1.0.0-rc1");
    check(kSideraEngineBanner == "SIDERA Engine 1.0.0-rc1", "Engine banner matches SIDERA Engine 1.0.0-rc1");
}

void testFormatCompatibilityAndMigration() {
    std::cout << "\n--- 2. Format Compatibility & Non-Destructive Migration ---\n";
    auto reg = FormatMigrationRegistry::createDefaultSidera10Registry();

    check(reg.checkCompatibility("sideraproj", 1) == FormatCompatibility::FullyCompatible,
          "sideraproj v1 is FullyCompatible");
    check(reg.checkCompatibility("siderascene", 1) == FormatCompatibility::FullyCompatible,
          "siderascene v1 is FullyCompatible");
    check(reg.checkCompatibility("siderasave", 1) == FormatCompatibility::FullyCompatible,
          "siderasave v1 is FullyCompatible");
    check(reg.checkCompatibility("siderasave", 99) == FormatCompatibility::UnsupportedFutureVersion,
          "Future schema version v99 rejected safely");
    check(reg.checkCompatibility("siderasave", 0) == FormatCompatibility::InvalidFormat,
          "Invalid schema version v0 rejected safely");

    // Test multi-step non-destructive migration with unknown field preservation
    reg.registerFormat({"testfmt", 1, 3});
    reg.registerMigration("testfmt", 1, [](VersionedDocument& d, std::string&) {
        d.fields["migrated_v2"] = "yes";
        return true;
    });
    reg.registerMigration("testfmt", 2, [](VersionedDocument& d, std::string&) {
        d.fields["migrated_v3"] = "yes";
        return true;
    });

    VersionedDocument doc{};
    doc.formatId = "testfmt";
    doc.schemaVersion = 1;
    doc.fields["name"] = "Alpha";
    doc.unknownFields["custom_plugin_tag"] = "preserve_me";

    std::string err;
    check(reg.migrateToCurrent(doc, err), "v1 -> v3 migration succeeds");
    check(doc.schemaVersion == 3, "Document schemaVersion updated to 3");
    check(doc.fields["migrated_v2"] == "yes" && doc.fields["migrated_v3"] == "yes",
          "All migration steps applied");
    check(doc.unknownFields["custom_plugin_tag"] == "preserve_me",
          "Unknown serialized fields preserved non-destructively");

    // Verify future version fails migration without mutating document
    VersionedDocument futureDoc{"testfmt", 5, {{"name", "Future"}}, {}};
    check(!reg.migrateToCurrent(futureDoc, err) && futureDoc.schemaVersion == 5,
          "Future version migration fails safely without mutating document");
}

void testReferenceProjectOpening() {
    std::cout << "\n--- 3. Editor Reference Project Opening ---\n";
    // Locate examples/rpg_reference_project from repo root or build/<cfg>
    std::filesystem::path refProj;
    for (const auto& candidate : {
             std::filesystem::path("examples/rpg_reference_project"),
             std::filesystem::path("../examples/rpg_reference_project"),
             std::filesystem::path("../../examples/rpg_reference_project")}) {
        if (std::filesystem::exists(candidate / "project.sideraproj")) {
            refProj = candidate;
            break;
        }
    }
    check(!refProj.empty(), "Located examples/rpg_reference_project/project.sideraproj");

    EditorApplication editor;
    check(editor.init(), "EditorApplication initialized");
    if (!refProj.empty()) {
        check(editor.openProject(refProj), "EditorApplication opened RPG_Reference_Project");
        check(editor.getActiveProject().name == "RPG_Reference_Project",
              "Active project name verified as RPG_Reference_Project");
        editor.closeProject();
        check(editor.getState() == EditorAppState::NoProject, "EditorApplication closed project cleanly");
    }
    editor.shutdown();
}

} // namespace

int main() {
    std::cout << "=== SIDERA Engine 1.0.0-rc1 — Release Validation Tests ===\n";
    testEngineVersion();
    testFormatCompatibilityAndMigration();
    testReferenceProjectOpening();

    if (g_failures > 0) {
        std::cerr << "\n[FAILED] " << g_failures << " test(s) failed.\n";
        return 1;
    }
    std::cout << "\n[ALL PASSED] SIDERA Engine 1.0.0-rc1 release validation passed.\n";
    return 0;
}
