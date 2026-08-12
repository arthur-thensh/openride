#!/usr/bin/env python3
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ANDROID_NS = "http://schemas.android.com/apk/res/android"
ET.register_namespace("android", ANDROID_NS)
A = "{" + ANDROID_NS + "}"
PACKAGE = "com.arthurthion.openride"

if len(sys.argv) != 2:
    raise SystemExit("usage: patch_android_project.py <android-project-dir>")

project = Path(sys.argv[1]).resolve()
app = project / "app"
manifest_path = app / "src/main/AndroidManifest.xml"
build_gradle = app / "build.gradle"

if not manifest_path.exists() or not build_gradle.exists():
    raise SystemExit("SDL Android project is incomplete")

tree = ET.parse(manifest_path)
root = tree.getroot()

permissions = {item.get(A + "name") for item in root.findall("uses-permission")}
for permission in (
    "android.permission.INTERNET",
    "android.permission.ACCESS_FINE_LOCATION",
    "android.permission.ACCESS_COARSE_LOCATION",
):
    if permission not in permissions:
        node = ET.Element("uses-permission")
        node.set(A + "name", permission)
        root.insert(0, node)

application = root.find("application")
if application is None:
    raise SystemExit("AndroidManifest.xml has no <application>")
application.set(A + "label", "OpenRide")

activity = None
for candidate in application.findall("activity"):
    name = candidate.get(A + "name", "")
    if "SDLActivity" in name:
        activity = candidate
        break
if activity is None:
    raise SystemExit("SDLActivity not found in AndroidManifest.xml")
activity.set(A + "name", PACKAGE + ".OpenRideActivity")
activity.set(A + "screenOrientation", "unspecified")

tree.write(manifest_path, encoding="utf-8", xml_declaration=True)

text = build_gradle.read_text()
if "project.hasProperty('BUILD_WITH_CMAKE')" not in text and 'project.hasProperty("BUILD_WITH_CMAKE")' not in text:
    raise SystemExit("Unsupported SDL Gradle template: BUILD_WITH_CMAKE switch not found")

# Keep SDL's template intact and only replace application identity/version values.
text = re.sub(r'namespace\s*=\s*["\']org\.libsdl\.app["\']',
              f'namespace = "{PACKAGE}"', text, count=1)
text = re.sub(r'namespace\s+["\']org\.libsdl\.app["\']',
              f'namespace = "{PACKAGE}"', text, count=1)

if re.search(r'\bapplicationId\s*(?:=\s*)?["\']', text):
    text = re.sub(r'\bapplicationId\s*(?:=\s*)?["\'][^"\']+["\']',
                  f'applicationId = "{PACKAGE}"', text, count=1)
else:
    text, count = re.subn(r'(defaultConfig\s*\{)',
                          r'\1\n        applicationId = "' + PACKAGE + r'"',
                          text,
                          count=1)
    if count != 1:
        raise SystemExit("Unable to locate defaultConfig in SDL build.gradle")

text = re.sub(r'\bversionCode\s*(?:=\s*)?\d+', 'versionCode = 23', text, count=1)
text = re.sub(r'\bversionName\s*(?:=\s*)?["\'][^"\']+["\']',
              'versionName = "0.22.1"', text, count=1)
build_gradle.write_text(text)
