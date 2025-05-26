# Noise-suppression-VST

This is a VST*/VST3 plugin for basic light-weight real-time noise suppresion using SpeexDSP, built with JUCE.
*My very first project I pubished on GitHub.*

## Preview

![image](https://github.com/user-attachments/assets/b9e4efab-1d4e-4d52-8755-e74ffb43d305)

## Features
 - Realtime noise suppression using open-source SpeexDSP
 - Works at most common host sample rates
 - Compatible with all major DAWs and audio hosts (Tested with Audacity, OBS Studio and Equalizer APO)

## Building the Plugin

To build the plugin:

1. Open `SpeexDSPNoiseSuppressor.jucer` with JUCE's Projucer.
2. Configure your desired exporter (e.g., Visual Studio, Xcode).
3. Save and open the project in your chosen IDE.
4. Build the project to generate the plugin binaries.

*Note: I was working with path C:\SpeexDSP_VST where Projucer, my VST2 SDK longside the project folder is located*

## Credits & Licenses
- This plugin uses [SpeexDSP](https://github.com/xiph/speexdsp) © Xiph.Org Foundation (BSD-3-Clause).
- Built with [JUCE](https://juce.com), subject to its license.

This code is released under GPLv3 lisense
See the `LICENSE` file for details.

## VST2 Support Disclaimer

Due to licensing restrictions from Steinberg, the VST2 SDK cannot be distributed with this project.
Building VST2 versions of this plugin is only possible if you already have the VST2 SDK from a previous license agreement with Steinberg.

No VST2 SDK files are included in this repository, and I cannot provide them.
VST3 build is fully supported and open source.

## Tools
This project was made with the help from ChatGPT 4.1 by OpenAI
