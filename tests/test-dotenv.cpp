#include <catch2/catch_test_macros.hpp>
#include "dotenv.hpp"

#include <string>
#include <vector>

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

TEST_CASE("dotenv_names lists the base variable and its named variants")
{
    std::string text = "OTHER=1\nGEMINI_API_KEY=a\nGEMINI_API_KEY_STUDIO=b\n"
                       "GEMINI_API_KEYRING=c\nGEMINI_API_KEY_EMPTY=\n"
                       "GEMINI_API_KEY_STUDIO=b2\n";
    std::vector<std::string> names = lt::dotenv_names(text, "GEMINI_API_KEY");
    REQUIRE(names == std::vector<std::string>{"GEMINI_API_KEY",
                                              "GEMINI_API_KEY_STUDIO"});
    REQUIRE(lt::dotenv_names("", "GEMINI_API_KEY").empty());
}

TEST_CASE("env_name_with_label builds a variable name from a key label")
{
    REQUIRE(lt::env_name_with_label("GEMINI_API_KEY", "") == "GEMINI_API_KEY");
    REQUIRE(lt::env_name_with_label("GEMINI_API_KEY", "default") ==
            "GEMINI_API_KEY");
    REQUIRE(lt::env_name_with_label("GEMINI_API_KEY", "studio") ==
            "GEMINI_API_KEY_STUDIO");
    REQUIRE(lt::env_name_with_label("GEMINI_API_KEY", " My key-2 ") ==
            "GEMINI_API_KEY_MY_KEY_2");
    REQUIRE(lt::env_name_with_label("GEMINI_API_KEY", "!!!") == "GEMINI_API_KEY");
}

TEST_CASE("env_label is the readable name of a key variable")
{
    REQUIRE(lt::env_label("GEMINI_API_KEY", "GEMINI_API_KEY").empty());
    REQUIRE(lt::env_label("GEMINI_API_KEY", "GEMINI_API_KEY_STUDIO") == "studio");
    REQUIRE(lt::env_label("GEMINI_API_KEY", "GEMINI_API_KEY_MY_KEY_2") ==
            "my_key_2");
}

TEST_CASE("env_name_belongs accepts only the base variable and its variants")
{
    REQUIRE(lt::env_name_belongs("GEMINI_API_KEY", "GEMINI_API_KEY"));
    REQUIRE(lt::env_name_belongs("GEMINI_API_KEY", "GEMINI_API_KEY_STUDIO"));
    REQUIRE_FALSE(lt::env_name_belongs("GEMINI_API_KEY", "GEMINI_API_KEYRING"));
    REQUIRE_FALSE(lt::env_name_belongs("GEMINI_API_KEY", "OTHER"));
    REQUIRE_FALSE(lt::env_name_belongs("GEMINI_API_KEY", ""));
}
