#pragma once
#include <array>
#include <vector>

#include "../Activity.h"

class BlackjackActivity final : public Activity {
 public:
  explicit BlackjackActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Blackjack", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  // Card representation: 0-51 maps to rank (0-12) and suit (0-3)
  struct Card {
    uint8_t rank;  // 0=A, 1=2, ..., 9=10, 10=J, 11=Q, 12=K
    uint8_t suit;  // 0=Spades, 1=Hearts, 2=Diamonds, 3=Clubs
  };

  enum class GameState { BETTING, DEALING, PLAYER_TURN, DEALER_TURN, RESULT };

  enum class ResultType { NONE, PLAYER_WIN, DEALER_WIN, PUSH, BLACKJACK };

  // Deck
  std::array<Card, 52> deck{};
  int deckIndex = 0;

  // Hands
  std::vector<Card> playerHand;
  std::vector<Card> dealerHand;

  // Chips & betting
  static constexpr int kStartingChips = 1000;
  static constexpr int kBetIncrements[] = {10, 25, 50, 100, 250};
  static constexpr int kNumBetIncrements = 5;
  int chips = kStartingChips;
  int currentBet = 0;
  int lastBet = 0;  // remembers bet amount for result display (currentBet resets after payout)
  int betIncrementIndex = 2;  // start at 50

  // State
  GameState state = GameState::BETTING;
  ResultType result = ResultType::NONE;
  int wins = 0;
  int losses = 0;
  int pushes = 0;
  int handsPlayed = 0;

  // Methods
  void initDeck();
  void shuffleDeck();
  Card drawCard();
  int handValue(const std::vector<Card>& hand) const;
  bool isSoft(const std::vector<Card>& hand) const;
  void placeBet(int amount);
  void dealInitialCards();
  void playerHit();
  void dealerPlay();
  void resolveRound();
  void payout();

  // Rendering helpers
  void drawCard(GfxRenderer& r, int x, int y, const Card& card, bool faceDown = false) const;
  void drawHand(GfxRenderer& r, int x, int y, const std::vector<Card>& hand, bool hideFirst = false) const;
  void drawChipInfo(GfxRenderer& r) const;
  void drawBettingScreen(GfxRenderer& r) const;
  void drawHandValue(GfxRenderer& r, int x, int y, const std::vector<Card>& hand) const;
  void drawResultMessage(GfxRenderer& r) const;
  void drawButtonHints(GfxRenderer& r) const;

  static const char* rankStr(uint8_t rank);
  static const char* suitStr(uint8_t suit);
};
