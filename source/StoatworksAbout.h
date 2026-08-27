/*
 * Stoatworks Labs - About window data for Astronaught.
 *
 * ⚠️ HAND-WRITTEN PLACEHOLDER, not generated. The fleet's copies of this file
 * come from stoatworks-backend/scripts/sync-about.py, which reads the website's
 * projects.json -- and astronaught has no entry there yet, so sync-about.py
 * cannot generate it and the guide and page URLs below point at pages that do
 * not exist. Add the projects.json entry, then run the sync and let it
 * overwrite this file. Until then the About block's buttons are wrong on
 * purpose rather than by accident, and docs/NOTES.md says so.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "Astronaught";
    inline constexpr auto slug = "astronaught";
    inline constexpr auto hook = "A video signal put through a Space Echo";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "https://stoatworks-labs.com/software/astronaught/guide/";
    inline constexpr auto page = "https://stoatworks-labs.com/software/astronaught/";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/astronaught";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
