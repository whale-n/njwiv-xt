#include "BookshelfTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
// Cover Flow layout constants
constexpr int kCenterCoverMaxW = 170;
constexpr int kCenterCoverH = 230;
constexpr int kSideCoverMaxW = 100;
constexpr int kSideCoverH = 150;
constexpr int kCoverGap = 14;
constexpr int kSelBorderWidth = 2;
constexpr int kSelPadding = 5;
constexpr int kCornerRadius = 8;
constexpr int kTextGap = 8;
constexpr int kDotSize = 4;
constexpr int kDotSpacing = 10;
constexpr int kTopPad = 15;

// Card grid constants
constexpr int kCardCols = 2;
constexpr int kCardSpacing = 8;
constexpr int kCardHeight = 100;
constexpr int kCardCornerRadius = 8;
constexpr int kCardIconSize = 32;

const uint8_t* cardIconForName(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Games:
      return BookIcon;  // TODO: custom dice/gamepad icon
    case UIIcon::Stats:
      return RecentIcon;  // TODO: custom bar chart icon
    default:
      return nullptr;
  }
}
}  // namespace

// ---------- Header ----------

void BookshelfTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                                const char* subtitle) const {
  if (title != nullptr) {
    // Non-home screens: use standard Lyra header
    LyraTheme::drawHeader(renderer, rect, title, subtitle);
    return;
  }

  // Home screen: branded header with "njwiv library"
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = rect.x + rect.width - 12 - BookshelfMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, BookshelfMetrics::values.batteryWidth, BookshelfMetrics::values.batteryHeight},
                   showBatteryPercentage);

  // Brand name: "njwiv" bold, "library" regular
  const int textX = rect.x + BookshelfMetrics::values.contentSidePadding;
  const int textY = rect.y + 24;
  const int njwivW = renderer.getTextWidth(UI_12_FONT_ID, "njwiv", EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, textX, textY, "njwiv", true, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, textX + njwivW + 2, textY, "library", true);

  // Separator line
  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, 1, true);
}

// ---------- Cover Flow ----------

static void drawOneCover(GfxRenderer& renderer, const RecentBook& book, int x, int y, int maxW, int maxH,
                         int thumbHeight) {
  bool hasCover = false;

  if (!book.coverBmpPath.empty()) {
    const std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeight);
    FsFile file;
    if (Storage.openFileForRead("HOME", path, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const float bmpW = static_cast<float>(bitmap.getWidth());
        const float bmpH = static_cast<float>(bitmap.getHeight());
        const float ratio = bmpW / bmpH;
        const float tileRatio = static_cast<float>(maxW) / static_cast<float>(maxH);
        const float cropX = std::max(0.0f, 1.0f - (tileRatio / ratio));
        renderer.drawBitmap(bitmap, x, y, maxW, maxH, cropX);
        hasCover = true;
      }
      file.close();
    }
  }

  renderer.drawRect(x, y, maxW, maxH, true);

  if (!hasCover) {
    renderer.fillRect(x, y + maxH / 3, maxW, 2 * maxH / 3, true);
    const int iconSize = (maxH >= 200) ? 32 : 24;
    renderer.drawIcon(CoverIcon, x + (maxW - iconSize) / 2, y + (maxH - iconSize) / 2, iconSize, iconSize);
  }
}

void BookshelfTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                         const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                         bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                         std::function<bool()> storeCoverBuffer) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const int centerIdx = std::min(selectorIndex, bookCount - 1);
  const bool bookSelected = selectorIndex < bookCount;
  const int pageW = renderer.getScreenWidth();
  const int thumbH = BookshelfMetrics::values.homeCoverHeight;

  // Vertical layout
  const int coverY = rect.y + kTopPad;
  const int sideCoverY = coverY + (kCenterCoverH - kSideCoverH) / 2;

  // Horizontal layout (centered on screen)
  const int centerX = (pageW - kCenterCoverMaxW) / 2;
  const int leftX = centerX - kCoverGap - kSideCoverMaxW;
  const int rightX = centerX + kCenterCoverMaxW + kCoverGap;

  // Draw side covers first (visually behind center)
  if (centerIdx > 0) {
    drawOneCover(renderer, recentBooks[centerIdx - 1], leftX, sideCoverY, kSideCoverMaxW, kSideCoverH, thumbH);
  }
  if (centerIdx + 1 < bookCount) {
    drawOneCover(renderer, recentBooks[centerIdx + 1], rightX, sideCoverY, kSideCoverMaxW, kSideCoverH, thumbH);
  }

  // Draw center cover (large, prominent)
  drawOneCover(renderer, recentBooks[centerIdx], centerX, coverY, kCenterCoverMaxW, kCenterCoverH, thumbH);

  // Selection border when browsing books
  if (bookSelected) {
    renderer.drawRoundedRect(centerX - kSelPadding, coverY - kSelPadding, kCenterCoverMaxW + 2 * kSelPadding,
                             kCenterCoverH + 2 * kSelPadding, kSelBorderWidth, kCornerRadius, true);
  }

  // --- Text area below center cover ---
  int currentY = coverY + kCenterCoverH + kTextGap;
  const int maxTextW = pageW - 2 * BookshelfMetrics::values.contentSidePadding;
  const auto& book = recentBooks[centerIdx];

  // Title (UI_12 Bold, centered, up to 2 lines)
  const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  auto titleLines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), maxTextW, 2, EpdFontFamily::BOLD);
  for (const auto& line : titleLines) {
    const int tw = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, (pageW - tw) / 2, currentY, line.c_str(), true, EpdFontFamily::BOLD);
    currentY += titleLineH;
  }

  // Author + progress (SMALL_FONT, centered, single line)
  std::string subtitle;
  if (!book.author.empty()) {
    subtitle = book.author;
  }
  if (book.progressPercent >= 0) {
    if (!subtitle.empty()) subtitle += " · ";
    subtitle += std::to_string(book.progressPercent) + "%";
  }
  if (!subtitle.empty()) {
    const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);
    auto subLines = renderer.wrappedText(SMALL_FONT_ID, subtitle.c_str(), maxTextW, 1);
    if (!subLines.empty()) {
      const int sw = renderer.getTextWidth(SMALL_FONT_ID, subLines[0].c_str());
      renderer.drawText(SMALL_FONT_ID, (pageW - sw) / 2, currentY, subLines[0].c_str(), true);
      currentY += smallLineH;
    }
  }

  // Navigation dot indicators
  if (bookCount > 1) {
    const int maxDots = std::min(bookCount, 10);
    const int totalW = maxDots * kDotSize + (maxDots - 1) * (kDotSpacing - kDotSize);
    int dotX = (pageW - totalW) / 2;
    const int dotY = currentY + 6;

    for (int i = 0; i < maxDots; i++) {
      if (i == centerIdx) {
        renderer.fillRect(dotX, dotY, kDotSize, kDotSize, true);
      } else {
        renderer.drawRect(dotX, dotY, kDotSize, kDotSize, true);
      }
      dotX += kDotSpacing;
    }
  }

  // Cover Flow doesn't use buffer caching — cover positions change with each scroll
  coverRendered = false;
  coverBufferStored = false;
}

// ---------- Card Grid Menu ----------

void BookshelfTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                    const std::function<std::string(int index)>& buttonLabel,
                                    const std::function<UIIcon(int index)>& rowIcon) const {
  const int sidePad = BookshelfMetrics::values.contentSidePadding;
  const int totalWidth = rect.width - 2 * sidePad;
  const int cardWidth = (totalWidth - kCardSpacing) / kCardCols;

  // Center card grid vertically in available space
  const int totalRows = (buttonCount + kCardCols - 1) / kCardCols;
  const int gridHeight = totalRows * kCardHeight + (totalRows - 1) * kCardSpacing;
  const int yOffset = std::max(0, (rect.height - gridHeight) / 3);

  for (int i = 0; i < buttonCount; i++) {
    const int row = i / kCardCols;
    const int col = i % kCardCols;

    const int x = rect.x + sidePad + col * (cardWidth + kCardSpacing);
    const int y = rect.y + yOffset + row * (kCardHeight + kCardSpacing);

    const bool selected = (selectedIndex == i);

    if (selected) {
      renderer.fillRoundedRect(x, y, cardWidth, kCardHeight, kCardCornerRadius, Color::LightGray);
      renderer.drawRoundedRect(x, y, cardWidth, kCardHeight, 2, kCardCornerRadius, true);
    } else {
      renderer.drawRoundedRect(x, y, cardWidth, kCardHeight, 1, kCardCornerRadius, true);
    }

    // Icon + label centered vertically in card
    const int contentH = kCardIconSize + 6 + renderer.getLineHeight(UI_10_FONT_ID);
    const int contentTop = y + (kCardHeight - contentH) / 2;

    if (rowIcon) {
      const uint8_t* iconBitmap = cardIconForName(rowIcon(i));
      if (iconBitmap) {
        const int iconX = x + (cardWidth - kCardIconSize) / 2;
        renderer.drawIcon(iconBitmap, iconX, contentTop, kCardIconSize, kCardIconSize);
      }
    }

    // Label centered below icon
    const std::string label = buttonLabel(i);
    const int textW = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
    const int textX = x + (cardWidth - textW) / 2;
    const int textY = contentTop + kCardIconSize + 6;
    renderer.drawText(UI_10_FONT_ID, textX, textY, label.c_str(), true);
  }
}
