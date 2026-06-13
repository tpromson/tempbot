#ifndef BITMAPS_H
#define BITMAPS_H

#include <Arduino.h>

// Include all bitmap sets
#include "bitmaps_cat.h"
#include "bitmaps_chicken.h"
#include "bitmaps_fish.h"
#include "bitmaps_tree.h"

// --- Current Bitmap Selection ---
// Values: "cat", "chicken", "fish", "tree"
#define DEFAULT_BITMAP "cat"

static String currentBitmapName = DEFAULT_BITMAP;

struct BitmapSet {
  const unsigned char (*frames)[512];  // Pointer to 2D array
  int frameCount;
  int frameSize;
};

// All available bitmap sets
static const BitmapSet bitmapSets[] = {
  { frames_cat, FRAME_COUNT_CAT, 512 },
  { frames_chicken, FRAME_COUNT_CHICKEN, 512 },
  { frames_fish, FRAME_COUNT_FISH, 512 },
  { frames_tree, FRAME_COUNT_TREE, 512 }
};

static const char* bitmapNames[] = {
  "cat",
  "chicken", 
  "fish",
  "tree"
};

static const int BITMAP_SET_COUNT = 4;

// Current selected frames
static const unsigned char (*currentFrames)[512] = frames_cat;
static int currentFrameCount = FRAME_COUNT_CAT;
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
  Serial.print("Fallback to: ");
  Serial.println(DEFAULT_BITMAP);
  // Fallback to default
  setBitmap(DEFAULT_BITMAP);
}

const char* getCurrentBitmapName() {
  return currentBitmapName.c_str();
}

#endif
