/**
 * BootActivity — Dynamic Boot Screen with Random Background
 * ==========================================================
 * Modified to load a random BMP from the sleep screen directory
 * as background, then overlay a white "loading card" with the
 * njwiv library logo, "loading..." text, and version string.
 *
 * Falls back to the original centered-logo boot screen when no
 * sleep screen images are found on the SD card.
 *
 * The sleep screen directory is scanned in the same order as
 * SleepActivity: /.sleep (hidden, preferred) then /sleep.
 * This reuses the same image pool the user has already set up.
 *
 * Only BW rendering is used (no greyscale multi-pass) to keep
 * boot time fast — the background is purely atmospheric.
 */

#include "BootActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <string>
#include <vector>

#include "CrossPointState.h"
#include "fontIds.h"
#include "images/NjwivLogo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const bool hasBackground = tryRenderRandomBackground();

  if (!hasBackground) {
    // No sleep screen images found — use clean white background
    renderer.clearScreen();
  }

  renderLoadingCard();
  renderer.displayBuffer();
}

bool BootActivity::tryRenderRandomBackground() const {
  // Scan for sleep screen directories (same order as SleepActivity)
  const char* sleepDir = nullptr;
  auto dir = Storage.open("/.sleep");
  if (dir && dir.isDirectory()) {
    sleepDir = "/.sleep";
  } else {
    if (dir) dir.close();
    dir = Storage.open("/sleep");
    if (dir && dir.isDirectory()) {
      sleepDir = "/sleep";
    }
  }

  if (!sleepDir) {
    if (dir) dir.close();
    return false;
  }

  // Collect all valid BMP files in the directory
  std::vector<std::string> files;
  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }
    file.getName(name, sizeof(name));
    auto filename = std::string(name);
    if (filename[0] == '.') {
      file.close();
      continue;
    }

    if (!FsHelpers::hasBmpExtension(filename)) {
      file.close();
      continue;
    }

    // Quick header check to verify it's a valid BMP
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
      LOG_DBG("BOOT", "Skipping invalid BMP: %s", name);
      file.close();
      continue;
    }

    files.emplace_back(filename);
    file.close();
  }

  const auto numFiles = files.size();
  if (numFiles == 0) {
    dir.close();
    return false;
  }

  // Reuse the same image that was shown at sleep (visual continuity:
  // sleep painting → boot with loading card overlay on same painting).
  // Falls back to random if lastSleepImage is unset or out of range.
  size_t imageIndex;
  if (APP_STATE.lastSleepImage != UINT8_MAX &&
      APP_STATE.lastSleepImage < numFiles) {
    imageIndex = APP_STATE.lastSleepImage;
    LOG_DBG("BOOT", "Reusing sleep image index %d", imageIndex);
  } else {
    imageIndex = random(numFiles);
    LOG_DBG("BOOT", "No valid sleep image index, random pick %d", imageIndex);
  }

  // Open and render the chosen image
  const auto filepath =
      std::string(sleepDir) + "/" + files[imageIndex];
  FsFile file;
  if (!Storage.openFileForRead("BOOT", filepath, file)) {
    LOG_ERR("BOOT", "Failed to open: %s", filepath.c_str());
    dir.close();
    return false;
  }

  LOG_DBG("BOOT", "Boot background: %s", files[imageIndex].c_str());

  Bitmap bitmap(file, true);  // enable dithering
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    LOG_ERR("BOOT", "Failed to parse BMP headers: %s", filepath.c_str());
    file.close();
    dir.close();
    return false;
  }

  renderBitmapBackground(bitmap);
  file.close();
  dir.close();
  return true;
}

void BootActivity::renderBitmapBackground(const Bitmap& bitmap) const {
  // Same scaling/centering logic as SleepActivity::renderBitmapSleepScreen
  // but BW-only (skip greyscale multi-pass) for speed.
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  int x, y;
  float cropX = 0, cropY = 0;

  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    float ratio = static_cast<float>(bitmap.getWidth()) /
                  static_cast<float>(bitmap.getHeight());
    const float screenRatio =
        static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      // Image wider than screen — crop sides to fill
      cropX = 1.0f - (screenRatio / ratio);
      ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) /
              static_cast<float>(bitmap.getHeight());
      x = 0;
      y = std::round((static_cast<float>(pageHeight) -
                       static_cast<float>(pageWidth) / ratio) /
                      2);
    } else {
      // Image taller than screen — crop top/bottom to fill
      cropY = 1.0f - (ratio / screenRatio);
      ratio = static_cast<float>(bitmap.getWidth()) /
              ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      x = std::round((static_cast<float>(pageWidth) -
                       static_cast<float>(pageHeight) * ratio) /
                      2);
      y = 0;
    }
  } else {
    // Small image — center it
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  renderer.clearScreen();
  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
}

void BootActivity::renderLoadingCard() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // ── Card dimensions ──
  // Sized to comfortably fit the logo + two lines of text
  const int cardW = 220;
  const int cardH = 220;
  const int cardX = (pageWidth - cardW) / 2;
  const int cardY = (pageHeight - cardH) / 2 - 30;  // slightly above center

  // ── White card body ──
  // Fill a solid white rectangle for the card background
  renderer.fillRect(cardX, cardY, cardW, cardH, false);  // false = white

  // ── Black border (2px) ──
  // Draw outline by filling thin black rectangles on each edge
  const int borderW = 2;
  renderer.fillRect(cardX, cardY, cardW, borderW, true);                      // top
  renderer.fillRect(cardX, cardY + cardH - borderW, cardW, borderW, true);    // bottom
  renderer.fillRect(cardX, cardY, borderW, cardH, true);                      // left
  renderer.fillRect(cardX + cardW - borderW, cardY, borderW, cardH, true);    // right

  // ── Inner border (1px, 6px margin inside) ──
  const int margin = 6;
  const int innerX = cardX + margin;
  const int innerY = cardY + margin;
  const int innerW = cardW - margin * 2;
  const int innerH = cardH - margin * 2;
  renderer.fillRect(innerX, innerY, innerW, 1, true);                         // top
  renderer.fillRect(innerX, innerY + innerH - 1, innerW, 1, true);            // bottom
  renderer.fillRect(innerX, innerY, 1, innerH, true);                         // left
  renderer.fillRect(innerX + innerW - 1, innerY, 1, innerH, true);            // right

  // ── njwiv library logo ──
  // Custom 120x120 bitmap with "njwiv" in Didot + "library" below
  const int logoSize = 120;
  const int logoX = cardX + (cardW - logoSize) / 2;
  const int logoY = cardY + (cardH - logoSize) / 2;  // vertically centered in card
  renderer.drawImage(NjwivLogo120, logoX, logoY, logoSize, logoSize);

  // ── "loading..." below the card ──
  // Draw a small white pill behind the text so it's visible on dark backgrounds
  const char* bootingText = "loading...";
  const int bootTextW = renderer.getTextWidth(SMALL_FONT_ID, bootingText);
  const int bootTextH = renderer.getLineHeight(SMALL_FONT_ID);
  const int bootTextX = (pageWidth - bootTextW) / 2;
  const int bootTextY = cardY + cardH + 16;
  const int bootPad = 6;
  renderer.fillRect(bootTextX - bootPad, bootTextY - bootPad / 2,
                    bootTextW + bootPad * 2, bootTextH + bootPad, false);
  renderer.drawCenteredText(SMALL_FONT_ID, bootTextY, bootingText);

  // ── Version string at bottom-right of screen ──
  // White pill behind version text for visibility on dark backgrounds
  const int versionW =
      renderer.getTextWidth(SMALL_FONT_ID, CROSSPOINT_VERSION);
  const int versionH = renderer.getLineHeight(SMALL_FONT_ID);
  const int versionX = pageWidth - versionW - 15;
  const int versionY = pageHeight - 25;
  const int verPad = 4;
  renderer.fillRect(versionX - verPad, versionY - verPad / 2,
                    versionW + verPad * 2, versionH + verPad, false);
  renderer.drawText(SMALL_FONT_ID, versionX, versionY,
                    CROSSPOINT_VERSION, true);
}
