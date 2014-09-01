#include "test.hh"

#include <utils.hh>
#include <string>

TESTSUITE(utils)
{
	TEST(escapeHtml)
	{
		char buf[8192];

		memset(buf, '&', sizeof(buf));
		buf[sizeof(buf) - 1] = '\0';

		std::string s = escape_html(buf);
		// Should have been truncated
		ASSERT_TRUE(s.size() < 4096);

		ASSERT_TRUE(s.find("&amp;") != std::string::npos);
	}

	TEST(escapeJson)
	{
		std::string a = "var kalle=1;";
		std::string b = "var kalle='1';";
		std::string c = "var a='\\hej';";

		std::string s;

		s = escape_json(a);
		ASSERT_TRUE(s == a);

		s = escape_json(b);
		ASSERT_TRUE(s == "var kalle=\\'1\\';");

		s = escape_json(c);
		ASSERT_TRUE(s == "var a=\\'\\\\hej\\';");
	}
}
