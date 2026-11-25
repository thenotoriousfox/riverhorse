// SPDX-FileCopyrightText: 2019-2023 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#include "common/string_util.h"
#include <gtest/gtest.h>

TEST(StringUtil, Ellipsise)
{
  ASSERT_EQ(StringUtil::Ellipsise("HelloWorld", 6, "..."), "Hel...");
  ASSERT_EQ(StringUtil::Ellipsise("HelloWorld", 7, ".."), "Hello..");
  ASSERT_EQ(StringUtil::Ellipsise("HelloWorld", 20, ".."), "HelloWorld");
  ASSERT_EQ(StringUtil::Ellipsise("", 20, "..."), "");
  ASSERT_EQ(StringUtil::Ellipsise("Hello", 10, "..."), "Hello");
}

TEST(StringUtil, EllipsiseInPlace)
{
  std::string s;
  s = "HelloWorld";
  StringUtil::EllipsiseInPlace(s, 6, "...");
  ASSERT_EQ(s, "Hel...");
  s = "HelloWorld";
  StringUtil::EllipsiseInPlace(s, 7, "..");
  ASSERT_EQ(s, "Hello..");
  s = "HelloWorld";
  StringUtil::EllipsiseInPlace(s, 20, "..");
  ASSERT_EQ(s, "HelloWorld");
  s = "";
  StringUtil::EllipsiseInPlace(s, 20, "...");
  ASSERT_EQ(s, "");
  s = "Hello";
  StringUtil::EllipsiseInPlace(s, 10, "...");
  ASSERT_EQ(s, "Hello");
}

TEST(StringUtil, EncodeBase64)
{
  ASSERT_EQ(StringUtil::EncodeBase64(""), "");
  ASSERT_EQ(StringUtil::EncodeBase64("f"), "Zg==");
  ASSERT_EQ(StringUtil::EncodeBase64("fo"), "Zm8=");
  ASSERT_EQ(StringUtil::EncodeBase64("foo"), "Zm9v");
  ASSERT_EQ(StringUtil::EncodeBase64("user:p\xc3\xa4ss"), "dXNlcjpww6Rzcw==");
}

TEST(StringUtil, URLComponent)
{
  ASSERT_EQ(StringUtil::EncodeURLComponent("Crash Bandicoot - Warped (USA)_1.mcd"),
            "Crash%20Bandicoot%20-%20Warped%20%28USA%29_1.mcd");
  ASSERT_EQ(StringUtil::DecodeURLComponent("Crash%20Bandicoot%20-%20Warped%20%28USA%29_1.mcd"),
            "Crash Bandicoot - Warped (USA)_1.mcd");
  ASSERT_EQ(StringUtil::DecodeURLComponent(StringUtil::EncodeURLComponent("a/b?c=d&e")), "a/b?c=d&e");
  ASSERT_EQ(StringUtil::DecodeURLComponent("100%"), "100%");
}
