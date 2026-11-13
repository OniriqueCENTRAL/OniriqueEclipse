#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "ui/OniriquePresets.h"

//==============================================================================
//  Presets d'usine d'ECLIPSE, rangés par usage pour le navigateur de la barre du haut. Des points de départ :
//  un réglage non cité reprend sa valeur par défaut.
//==============================================================================
namespace eclipsepresets
{
enum Tags { mixbus = 1, vocal = 2, beat = 4, master = 8, ms = 16 };

inline juce::StringArray tagNames() { return { "Mix bus", "Vocals", "Instrumental", "Master", "M/S" }; }

inline std::vector<onirique::PresetBank::Stock> stockPresets()
{
    using namespace params;
    auto n = [] (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); };
    return {
        { "INIT", 0, {} },
        { n ("RAP BUS \xc2\xb7 WEIGHT & AIR"), mixbus,
          { { lowFreq, 60.0f }, { lowBoost, 4.0f }, { lowAtten, 2.0f }, { contour, 2.5f }, { contourFreq, 450.0f },
            { highFreq, 12000.0f }, { highBoost, 3.0f }, { hpfOn, 1.0f }, { hpfFreq, 24.0f }, { character, 45.0f }, { xformer, 25.0f } } },
        { n ("RAP BUS \xc2\xb7 KNOCK"), mixbus,
          { { lowFreq, 80.0f }, { lowBoost, 5.0f }, { lowAtten, 3.5f }, { midFreq, 2500.0f }, { midGain, 1.5f }, { midQ, 0.7f },
            { contour, 2.0f }, { contourFreq, 300.0f }, { hpfOn, 1.0f }, { hpfFreq, 28.0f }, { character, 60.0f } } },
        { n ("RAP BUS \xc2\xb7 WIDE TOP"), mixbus | ms,
          { { msLink, 0.0f }, { offsetTarget, 0.0f }, { highFreq, 10000.0f }, { highBoost, 2.0f }, { highOffset, 3.0f },
            { lowOffset, -4.0f }, { lowFreq, 150.0f }, { width, 115.0f }, { character, 35.0f } } },
        { n ("VOCAL BUS \xc2\xb7 SILK"), vocal,
          { { lowFreq, 120.0f }, { lowAtten, 2.5f }, { contour, 2.0f }, { contourFreq, 500.0f }, { midFreq, 3200.0f },
            { midGain, 1.5f }, { midQ, 0.6f }, { highFreq, 14000.0f }, { highBoost, 4.0f }, { hpfOn, 1.0f }, { hpfFreq, 70.0f },
            { character, 35.0f } } },
        { n ("VOCAL BUS \xc2\xb7 FRONT"), vocal,
          { { midFreq, 1800.0f }, { midGain, 2.5f }, { midQ, 0.8f }, { contour, -2.0f }, { contourFreq, 1500.0f },
            { highFreq, 9000.0f }, { highBoost, 2.0f }, { lowFreq, 200.0f }, { lowAtten, 3.0f }, { hpfOn, 1.0f }, { hpfFreq, 90.0f },
            { character, 50.0f } } },
        { n ("VOCAL BUS \xc2\xb7 DE-MUD"), vocal,
          { { contour, 4.0f }, { contourFreq, 350.0f }, { highFreq, 11000.0f }, { highBoost, 2.5f }, { hpfOn, 1.0f },
            { hpfFreq, 80.0f }, { character, 30.0f } } },
        { n ("BEAT \xc2\xb7 VINTAGE GLOW"), beat,
          { { lowFreq, 50.0f }, { lowBoost, 3.0f }, { midFreq, 900.0f }, { midGain, 1.5f }, { midQ, 0.5f }, { highFreq, 7000.0f },
            { highAtten, 2.0f }, { lpfOn, 1.0f }, { lpfFreq, 16000.0f }, { character, 75.0f }, { xformer, 45.0f } } },
        { n ("BEAT \xc2\xb7 CLEAN SIDES"), beat | ms,
          { { msLink, 0.0f }, { offsetTarget, 0.0f }, { lowFreq, 180.0f }, { lowOffset, -8.0f }, { contourOffset, 2.5f },
            { contourFreq, 600.0f }, { highOffset, 2.0f }, { highFreq, 9000.0f }, { character, 30.0f } } },
        { n ("BEAT \xc2\xb7 808 CENTER"), beat | ms,
          { { msLink, 0.0f }, { offsetTarget, 1.0f }, { lowFreq, 55.0f }, { lowBoost, 2.0f }, { lowOffset, 3.0f },
            { width, 108.0f }, { hpfOn, 1.0f }, { hpfFreq, 22.0f }, { character, 50.0f } } },
        { n ("MASTER \xc2\xb7 GENTLE CURVE"), master,
          { { lowFreq, 40.0f }, { lowBoost, 1.5f }, { lowAtten, 1.0f }, { contour, 1.0f }, { contourFreq, 800.0f },
            { highFreq, 16000.0f }, { highBoost, 1.5f }, { hpfOn, 1.0f }, { hpfFreq, 18.0f }, { character, 25.0f } } },
        { n ("MASTER \xc2\xb7 M/S POLISH"), master | ms,
          { { msLink, 0.0f }, { offsetTarget, 2.0f }, { lowFreq, 100.0f }, { lowOffset, -3.0f }, { highFreq, 12000.0f },
            { highBoost, 1.0f }, { highOffset, 2.0f }, { contour, 1.0f }, { contourFreq, 2500.0f }, { character, 25.0f } } },
        { n ("MASTER \xc2\xb7 TAPE DARK"), master,
          { { highFreq, 6000.0f }, { highAtten, 2.5f }, { lowFreq, 70.0f }, { lowBoost, 2.0f }, { lpfOn, 1.0f }, { lpfFreq, 18000.0f },
            { character, 70.0f }, { xformer, 40.0f } } },
    };
}
} // namespace eclipsepresets
