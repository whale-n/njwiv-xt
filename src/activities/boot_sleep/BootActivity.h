#pragma once
#include "../Activity.h"

class Bitmap;

class BootActivity final : public Activity {
 public:
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Boot", renderer, mappedInput) {}
  void onEnter() override;

 private:
  /// Try to load a random BMP from /.sleep or /sleep as background.
  /// Returns true if a background was rendered to the framebuffer.
  bool tryRenderRandomBackground() const;

  /// Render a Bitmap to the framebuffer (BW only, no greyscale multi-pass).
  /// Uses the same scaling/centering logic as SleepActivity::renderBitmapSleepScreen
  /// but skips greyscale passes for faster boot.
  void renderBitmapBackground(const Bitmap& bitmap) const;

  /// Draw the white "loading card" overlay with logo, name, and version.
  void renderLoadingCard() const;
};
