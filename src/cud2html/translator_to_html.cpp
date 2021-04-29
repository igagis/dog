#include "translator_to_html.hpp"

using namespace curlydoc;

translator_to_html::translator_to_html(std::string&& file_name) :
		translator(std::move(file_name))
{
	// TODO:
}

void translator_to_html::handle_space(){
	this->ss << ' ';
}

void translator_to_html::handle_word(const std::string& word){
	this->ss << word;
}

void translator_to_html::handle_paragraph(const treeml::tree_ext& tree){
	this->ss << '\n' << "<p>";
	this->translate(tree.children);
	this->ss << "</p>";
}

void translator_to_html::handle_bold(const treeml::tree_ext& tree){
	this->ss << "<b>";
	this->translate(tree.children);
	this->ss << "</b>";
}

void translator_to_html::handle_italic(const treeml::tree_ext& tree){
	this->ss << "<i>";
	this->translate(tree.children);
	this->ss << "</i>";
}

void translator_to_html::handle_underline(const treeml::tree_ext& tree){
	this->ss << "<u>";
	this->translate(tree.children);
	this->ss << "</u>";
}

void translator_to_html::handle_strikethrough(const treeml::tree_ext& tree){
	this->ss << "<s>";
	this->translate(tree.children);
	this->ss << "</s>";
}

void translator_to_html::handle_header1(const treeml::tree_ext& tree){
	this->ss << '\n' << "<h1>";
	this->translate(tree.children);
	this->ss << "</h1>";
}

void translator_to_html::handle_header2(const treeml::tree_ext& tree){
	this->ss << '\n' << "<h2>";
	this->translate(tree.children);
	this->ss << "</h2>";
}

void translator_to_html::handle_header3(const treeml::tree_ext& tree){
	this->ss << '\n' << "<h3>";
	this->translate(tree.children);
	this->ss << "</h3>";
}

void translator_to_html::handle_header4(const treeml::tree_ext& tree){
	this->ss << '\n' << "<h4>";
	this->translate(tree.children);
	this->ss << "</h4>";
}

void translator_to_html::handle_header5(const treeml::tree_ext& tree){
	this->ss << '\n' << "<h5>";
	this->translate(tree.children);
	this->ss << "</h5>";
}

void translator_to_html::handle_header6(const treeml::tree_ext& tree){
	this->ss << '\n' << "<h6>";
	this->translate(tree.children);
	this->ss << "</h6>";
}
