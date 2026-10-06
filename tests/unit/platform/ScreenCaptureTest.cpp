#include <gtest/gtest.h>
#include "platform/screen/ScreenCapture.h"

namespace {

TEST(ScreenCaptureTest, CaptureRegionValid) {
    // Small 10x10 capture of the screen
    RECT region{ 0, 0, 10, 10 };
    auto result = ScreenCapture::captureRegion(region);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->width, 10);
    EXPECT_EQ(result->height, 10);
    EXPECT_FALSE(result->pngBytes.empty());

    // Verify PNG magic header: 0x89 'P' 'N' 'G'
    ASSERT_GE(result->pngBytes.size(), 4U);
    EXPECT_EQ(result->pngBytes[0], 0x89);
    EXPECT_EQ(result->pngBytes[1], 'P');
    EXPECT_EQ(result->pngBytes[2], 'N');
    EXPECT_EQ(result->pngBytes[3], 'G');
}

TEST(ScreenCaptureTest, CaptureRegionRejectsZeroOrInvertedDimensions) {
    // Zero dimensions
    RECT zeroRegion{ 50, 50, 50, 50 };
    auto resZero = ScreenCapture::captureRegion(zeroRegion);
    EXPECT_FALSE(resZero.has_value());
    EXPECT_TRUE(resZero.error().contains("Invalid capture dimensions"));

    // Inverted dimensions
    RECT invertedRegion{ 100, 100, 50, 50 };
    auto resInverted = ScreenCapture::captureRegion(invertedRegion);
    EXPECT_FALSE(resInverted.has_value());
    EXPECT_TRUE(resInverted.error().contains("Invalid capture dimensions"));
}

TEST(ScreenCaptureTest, CaptureWindowNullHwnd) {
    auto result = ScreenCapture::captureWindow(static_cast<HWND>(nullptr));
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().contains("Invalid HWND"));
}

TEST(ScreenCaptureTest, CaptureWindowNonExistentTitle) {
    auto result = ScreenCapture::captureWindow("GhostWindowThatDoesNotExist_12345");
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().contains("not found"));
}

TEST(ScreenCaptureTest, ToBase64Encoding) {
    std::vector<uint8_t> data = { 'H', 'e', 'l', 'l', 'o' };
    std::string b64 = ScreenCapture::toBase64(data);
    EXPECT_EQ(b64, "SGVsbG8=");
}

} // namespace
