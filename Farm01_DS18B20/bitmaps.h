#ifndef BITMAPS_H
#define BITMAPS_H

#include <Arduino.h>

// --- Bitmap Sets Available ---
#include "bitmaps_cat.h"      // Cat (default)
#include "bitmaps_chicken.h"  // Chicken placeholder
#include "bitmaps_fish.h"     // Fish placeholder
#include "bitmaps_tree.h"      // Tree placeholder

// --- Current Bitmap Selection ---
// Values: "cat", "chicken", "fish", "tree"
#define DEFAULT_BITMAP "cat"

static String currentBitmapName = DEFAULT_BITMAP;

struct BitmapSet {
  const unsigned char* frames;
  int frameCount;
  int frameSize;
};

static const BitmapSet bitmapSets[] = {
  { frames_cat, FRAME_COUNT_cat, 512 },
  { frames_chicken, FRAME_COUNT_chicken, 512 },
  { frames_fish, FRAME_COUNT_fish, 512 },
  { frames_tree, FRAME_COUNT_tree, 512 }
};

static const char* bitmapNames[] = {
  "cat",
  "chicken", 
  "fish",
  "tree"
};

static const int BITMAP_SET_COUNT = 4;

// Current selected frames
static const unsigned char* currentFrames = frames_cat;
static int currentFrameCount = FRAME_COUNT_cat;
static int currentFrameSize = 512;

// --- Functions ---

void setBitmap(const char* name) {
  for (int i = 0; i < BITMAP_SET_COUNT; i++) {
    if (strcmp(name, bitmapNames[i]) == 0) {
      currentBitmapName = String(name);
      currentFrames = bitmapSets[i].frames;
      currentFrameCount = bitmapSets[i].frameCount;
      currentFrameSize = bitmapSets[i].frameSize;
      Serial.print("Bitmap set to: ");
      Serial.println(name);
      return;
    }
  }
  Serial.print("Unknown bitmap: ");
  Serial.println(name);
}

const char* getCurrentBitmapName() {
  return currentBitmapName.c_str();
}

#endif
