#include <catch2/catch_test_macros.hpp>
#include "dotenv.hpp"

#include <string>

using lt::dotenv_get;
using lt::dotenv_set;

TEST_CASE("dotenv_get reads a plain KEY=VALUE line")
{
    REQUIRE(dotenv_get("GEMINI_API_KEY=abc123\n", "GEMINI_API_KEY") == "abc123");
    REQUIRE(dotenv_get("GEMINI_API_KEY=abc123", "GEMINI_API_KEY") == "abc123");
}

TEST_CASE("dotenv_get returns empty for a missing name or empty text")
{
    REQUIRE(dotenv_get("", "GEMINI_API_KEY").empty());
    REQUIRE(dotenv_get("OTHER=1\n", "GEMINI_API_KEY").empty());
    REQUIRE(dotenv_get("GEMINI_API_KEY_OLD=1\n", "GEMINI_API_KEY").empty());
}

TEST_CASE("dotenv_get tolerates spaces, quotes, export, comments and CRLF")
{
    REQUIRE(dotenv_get("  GEMINI_API_KEY = abc \n", "GEMINI_API_KEY") == "abc");
    REQUIRE(dotenv_get("GEMINI_API_KEY=\"abc\"\n", "GEMINI_API_KEY") == "abc");
    REQUIRE(dotenv_get("GEMINI_API_KEY='abc'\n", "GEMINI_API_KEY") == "abc");
    REQUIRE(dotenv_get("export GEMINI_API_KEY=abc\n", "GEMINI_API_KEY") == "abc");
    REQUIRE(dotenv_get("GEMINI_API_KEY=abc # my key\n", "GEMINI_API_KEY") == "abc");
    REQUIRE(dotenv_get("# GEMINI_API_KEY=old\nGEMINI_API_KEY=abc\r\n",
                       "GEMINI_API_KEY") == "abc");
}

TEST_CASE("dotenv_get takes the last definition")
{
    REQUIRE(dotenv_get("GEMINI_API_KEY=a\nGEMINI_API_KEY=b\n",
                       "GEMINI_API_KEY") == "b");
}

TEST_CASE("dotenv_set adds the variable to an empty file")
{
    REQUIRE(dotenv_set("", "GEMINI_API_KEY", "abc") == "GEMINI_API_KEY=abc\n");
}

TEST_CASE("dotenv_set replaces the variable and keeps everything else")
{
    std::string text = "# my keys\nOTHER=1\nGEMINI_API_KEY=old\nLAST=2";
    std::string out = dotenv_set(text, "GEMINI_API_KEY", "new");
    REQUIRE(out == "# my keys\nOTHER=1\nLAST=2\nGEMINI_API_KEY=new\n");
    REQUIRE(dotenv_get(out, "GEMINI_API_KEY") == "new");
    REQUIRE(dotenv_get(out, "OTHER") == "1");
}

TEST_CASE("dotenv_set with an empty value removes the variable")
{
    REQUIRE(dotenv_set("GEMINI_API_KEY=abc\n", "GEMINI_API_KEY", "").empty());
    REQUIRE(dotenv_set("OTHER=1\nGEMINI_API_KEY=abc\n", "GEMINI_API_KEY", "") ==
            "OTHER=1\n");
}
