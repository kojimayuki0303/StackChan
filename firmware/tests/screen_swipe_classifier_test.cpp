/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <cstdlib>
#include <iostream>
#include <optional>

#include <hal/board/screen_swipe_classifier.h>
#include <hal/board/screen_swipe_direction.h>

namespace {

constexpr int kMinimumDistance = 50;

void expectDirection(std::optional<ScreenSwipeDirection> actual, std::optional<ScreenSwipeDirection> expected,
                      const char* label)
{
    if (actual != expected) {
        std::cerr << label << ": mismatch\n";
        std::exit(1);
    }
}

void testLeft()
{
    expectDirection(ClassifySwipe(-80, 5, kMinimumDistance), ScreenSwipeDirection::Left, "left");
}

void testRight()
{
    expectDirection(ClassifySwipe(80, -5, kMinimumDistance), ScreenSwipeDirection::Right, "right");
}

void testUp()
{
    expectDirection(ClassifySwipe(5, -80, kMinimumDistance), ScreenSwipeDirection::Up, "up");
}

void testDown()
{
    expectDirection(ClassifySwipe(-5, 80, kMinimumDistance), ScreenSwipeDirection::Down, "down");
}

void testBelowThreshold()
{
    expectDirection(ClassifySwipe(30, 0, kMinimumDistance), std::nullopt, "below threshold horizontal");
    expectDirection(ClassifySwipe(0, 30, kMinimumDistance), std::nullopt, "below threshold vertical");
}

void testExactTie()
{
    expectDirection(ClassifySwipe(60, 60, kMinimumDistance), std::nullopt, "exact tie positive");
    expectDirection(ClassifySwipe(-60, 60, kMinimumDistance), std::nullopt, "exact tie mixed sign");
}

void testDiagonalDominance()
{
    // Horizontal dominates when |dx| > |dy|, even if both exceed the threshold.
    expectDirection(ClassifySwipe(90, 60, kMinimumDistance), ScreenSwipeDirection::Right, "diagonal horizontal dominance");
    // Vertical dominates when |dy| > |dx|.
    expectDirection(ClassifySwipe(60, -90, kMinimumDistance), ScreenSwipeDirection::Up, "diagonal vertical dominance");
}

}  // namespace

int main()
{
    testLeft();
    testRight();
    testUp();
    testDown();
    testBelowThreshold();
    testExactTie();
    testDiagonalDominance();
    return 0;
}
