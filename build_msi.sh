#!/bin/bash

set -e

BUILD_OUTPUT="build/Release"
MSI_OUTPUT="build/VoiceTyper.msi"
PRODUCT_VERSION=$(cat VERSION | tr -d '[:space:]')

echo ""
echo "=== Building MSI installer ==="
wix build -o "$MSI_OUTPUT" -pdbtype none -d "BuildOutput=$(pwd)/$BUILD_OUTPUT" -d "ProductVersion=$PRODUCT_VERSION" packaging/VoiceTyper.wxs

echo ""
echo "=== Done ==="
echo "MSI created at: $MSI_OUTPUT"
