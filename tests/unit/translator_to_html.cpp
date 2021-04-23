#include <tst/set.hpp>
#include <tst/check.hpp>

#include <treeml/tree.hpp>

#include "../../src/cdoc2html/translator_to_html.hpp"


namespace{
tst::set set("traslator_to_html", [](auto& suite){
	suite.add("p_translates_to_html_p", [](){
		const auto input = treeml::read("p{hello world!}");

		curlydoc::translator_to_html tr;

		tr.translate(input.begin(), input.end());

		auto str = tr.ss.str();
		tst::check(str == "<p>hello world!</p>", SL) << "str = " << str;
	});
});
}
