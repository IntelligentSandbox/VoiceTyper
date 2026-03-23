#!/bin/bash

set -e

BUILD_OUTPUT="build/Release"
MSI_OUTPUT="build/VoiceTyper.msi"

echo ""
echo "=== Building MSI installer ==="
wix build -o "$MSI_OUTPUT" -d "BuildOutput=$(pwd)/$BUILD_OUTPUT" packaging/VoiceTyper.wxs

echo ""
echo "=== Done ==="
echo "MSI created at: $MSI_OUTPUT"
