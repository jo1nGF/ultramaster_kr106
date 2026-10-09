#!/usr/bin/env python3
"""Regenerates KR106Touch.jucer (Projucer project for the Android app).
Run from the repo root:  python3 android/gen_jucer.py
"""
import hashlib, os, glob
from xml.sax.saxutils import quoteattr

def rid(s):  # stable 6-char id
    return hashlib.md5(s.encode()).hexdigest()[:6]

def files(pattern): return sorted(glob.glob(pattern))

def file_el(path, compile_=False, resource=False, indent="      "):
    n = os.path.basename(path)
    return f'{indent}<FILE id="{rid(path)}" name={quoteattr(n)} compile="{int(compile_)}" resource="{int(resource)}" file={quoteattr(path)}/>'

def group(name, elems, indent="    "):
    return f'{indent}<GROUP id="{rid("grp"+name)}" name="{name}">\n' + "\n".join(elems) + f'\n{indent}</GROUP>'

src_cpp = files("Source/*.cpp")
src_h   = files("Source/*.h")
ctl_h   = files("Source/Controls/*.h")
dsp_h   = files("Source/DSP/*.h")
res = [
 "resources/img/kr106_background@2x.png","resources/img/knob@2x.png","resources/img/smallknob@2x.png",
 "resources/img/switch_3way@2x.png","resources/img/switch_3way_horizontal@2x.png","resources/img/led_red@2x.png",
 "resources/img/kr106_bender_gradient@2x.png","resources/img/transpose_chevron@2x.png","resources/img/new_knob@2x.png",
 "resources/img/slider_handle@2x.png","resources/fonts/Segment14.otf"]  # same list as CMake's KR106BinaryData
for r in res: assert os.path.exists(r), r

groups = [
  group("android", [file_el("android/Main.cpp", True)]),
  group("Source", [file_el(p, True) for p in src_cpp] + [file_el(p) for p in src_h]),
  group("Controls", [file_el(p) for p in ctl_h]),
  group("DSP", [file_el(p) for p in dsp_h]),
  group("Resources", [file_el(p, False, True) for p in res]),
]

mods = ["juce_audio_basics","juce_audio_devices","juce_audio_formats","juce_audio_processors",
        "juce_audio_processors_headless","juce_audio_utils","juce_core","juce_data_structures",
        "juce_dsp","juce_events","juce_graphics","juce_gui_basics","juce_gui_extra"]

defines = "\n".join(["KR106_TOUCH=1","JUCE_WEB_BROWSER=0","JUCE_USE_CURL=0","_USE_MATH_DEFINES"])
hdr = "Source\nSource/DSP\nSource/Controls\n."
modpaths = "\n".join(f'        <MODULEPATH id="{m}" path="libs/JUCE/modules"/>' for m in mods)

xml = f'''<?xml version="1.0" encoding="UTF-8"?>

<JUCERPROJECT id="{rid("proj")}" name="KR106Touch" projectType="guiapp" version="2.5.13"
              bundleIdentifier="org.kayrock.kr106touch" companyName="Kayrock"
              useAppConfig="0" addUsingNamespaceToJuceHeader="0" displaySplashScreen="0"
              reportAppUsage="0" cppLanguageStandard="17" jucerFormatVersion="1"
              headerPath={quoteattr(hdr)} defines={quoteattr(defines)}>
  <MAINGROUP id="{rid("main")}" name="KR106Touch">
{chr(10).join(groups)}
  </MAINGROUP>
  <EXPORTFORMATS>
    <ANDROIDSTUDIO targetFolder="Builds/Android" androidScreenOrientation="landscape"
                   androidMinimumSDK="24" androidInternetNeeded="0"
                   androidExternalWriteNeeded="0" androidReadMediaAudioPermission="0"
                   androidReadMediaImagesPermission="0" androidReadMediaVideoPermission="0"
                   androidPushNotifications="0" androidBluetoothScanNeeded="1"
                   androidBluetoothConnectNeeded="1"
                   extraCompilerFlags="-ffp-contract=fast -fno-math-errno">
      <CONFIGURATIONS>
        <CONFIGURATION name="Debug" isDebug="1" targetName="KR106Touch" androidArchitectures="arm64-v8a"/>
        <CONFIGURATION name="Release" isDebug="0" targetName="KR106Touch" androidArchitectures="arm64-v8a"/>
      </CONFIGURATIONS>
      <MODULEPATHS>
{modpaths}
      </MODULEPATHS>
    </ANDROIDSTUDIO>
  </EXPORTFORMATS>
  <MODULES>
''' + "\n".join(f'    <MODULE id="{m}" showAllCode="1" useLocalCopy="0" useGlobalPath="0"/>' for m in mods) + '''
  </MODULES>
  <JUCEOPTIONS JUCE_WEB_BROWSER="0" JUCE_USE_CURL="0" JUCE_USE_ANDROID_OBOE="1"/>
  <LIVE_SETTINGS/>
</JUCERPROJECT>
'''
open("KR106Touch.jucer", "w").write(xml)
print("wrote KR106Touch.jucer:", len(src_cpp), "cpp,", len(src_h)+len(ctl_h)+len(dsp_h), "headers,", len(res), "resources")
