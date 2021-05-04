#include "interpreter.hpp"

#include <utki/util.hpp>

using namespace curlydoc;

interpreter::exception::exception(const std::string& message) :
		std::invalid_argument(message + " at:")
{}

interpreter::exception::exception(const std::string& message, const std::string& file, const treeml::leaf_ext& leaf) :
		std::invalid_argument([&](){
			const auto& l = leaf.get_info().location;
			std::stringstream ss;
			ss << message << '\n';
			ss << "  " << file << ":" << l.line << ":" << l.offset << ": " << leaf.to_string();
			return ss.str();
		}())
{}

void interpreter::add_function(const std::string& name, function_type&& func){
	auto res = this->functions.insert(std::make_pair(std::move(name), std::move(func)));
	if(!res.second){
		std::stringstream ss;
		ss << "function '" << name << "' is already added";
		throw std::logic_error(ss.str());
	}
}

void interpreter::context::add(const std::string& name, treeml::forest_ext&& value){
	auto i = this->def.insert(
			std::make_pair(name, std::move(value))
		);
	if(!i.second){
		throw exception("variable name already exists in this context");
	}
}

interpreter::context::find_result interpreter::context::find(const std::string& name)const{
	auto i = this->def.find(name);
	if(i == this->def.end()){
		if(this->prev){
			return this->prev->find(name);
		}
		return {
			nullptr,
			*this
		};
	}
	ASSERT(this->prev)
	return {
		&i->second,
		*this->prev
	};
}

interpreter::context& interpreter::push_context(const context* prev){
	if(!prev){
		prev = &this->context_stack.back();
	}
	this->context_stack.push_back(context{prev});
	return this->context_stack.back();
}

interpreter::interpreter(std::unique_ptr<papki::file> file) :
		file_name_stack{file ? file->path() : std::string()},
		file(std::move(file))
{
	this->add_function("asis", [](const treeml::forest_ext& args){
		ASSERT(!args.empty()) // if there are no arguments, then it is not a function call

		return args;
	});

	this->add_function("", [this](const treeml::forest_ext& args){
		ASSERT(!args.empty()) // if there are no arguments, then it is not a function call

		treeml::forest_ext ret;
		treeml::tree_ext t(std::string(""));
		t.children = this->eval(args);
		ret.push_back(std::move(t));
		return ret;
	});

	this->add_function("def", [this](const treeml::forest_ext& args){
		ASSERT(!args.empty()) // if there are no arguments, then it is not a function call

		auto& ctx = this->push_context();

		for(const auto& c : args){
			try{
				ctx.add(c.value.to_string(), this->eval(c.children));
			}catch(exception& e){
				throw exception(e.what(), this->file_name_stack.back(), c.value);
			}
		}
		return treeml::forest_ext();
	});

	this->add_function("$", [this](const treeml::forest_ext& args){
		ASSERT(!args.empty()) // if there are no arguments, then it is not a function call

		ASSERT(!this->context_stack.empty())

		const auto& name = args.front().value.to_string();

		auto v = this->context_stack.back().find(name);

		if(!v.value){
			throw exception(std::string("variable '") + name +"' not found");
		}

		const auto& val = *v.value;

		if(args.size() >= 2){ // if access by index
			if(!args.front().children.empty()){
				throw exception("both ways of accessing array element specified (by index and by key)");
			}

			auto index = this->eval(std::next(args.begin()), args.end());

			if(index.size() != 1){
				throw exception("index evaluates to none or more than one value");
			}

			unsigned long i;
			try{
				i = std::stoul(index.front().value.to_string(), 0, 0);
			}catch(std::exception& e){
				throw exception(std::string("given index '") + index.front().value.to_string() + "' is not valid");
			}

			if(i >= v.value->size()){
				std::stringstream ss;
				ss << "index out of bounds (" << i << ")";
				throw exception(ss.str());
			}

			return treeml::forest_ext{val[i]};
		}else if(!args.front().children.empty()){ // access by key
			ASSERT(args.size() == 1)

			auto key = this->eval(args.front().children);

			if(key.size() != 1){
				throw exception("key evaluates to none or more than one value");
			}

			const auto& key_str = key.front().value.to_string();

			auto i = std::find(val.begin(), val.end(), key_str);
			if(i == val.end()){
				throw exception(std::string("key '") + key_str + "' not found");
			}

			return i->children;
		}else{ // whole variable value
			return val;
		}
	});

	this->add_function("for", [this](const treeml::forest_ext& args){
		ASSERT(!args.empty()) // if there are no arguments, then it is not a function call

		auto iter_name = args[0].value.to_string();
		auto iter_values = this->eval(args[0].children);

		treeml::forest_ext ret;

		for(const auto& i : iter_values){
			auto& ctx = this->push_context();
			utki::scope_exit context_scope_exit([this](){
				this->context_stack.pop_back();
			});

			try{
				ctx.add(iter_name, {i});
			}catch(exception&){
				ASSERT(false)
			}

			auto output = this->eval(std::next(args.begin()), args.end());

			ret.insert(ret.end(), std::make_move_iterator(output.begin()), std::make_move_iterator(output.end()));
		}

		return ret;
	});

	this->add_function("if", [this](const treeml::forest_ext& args){
		ASSERT(!args.empty()) // if there are no arguments, then it is not a function call

		treeml::forest_ext output = this->eval(args);

		this->if_flag_stack.back() = !output.empty();

		return treeml::forest_ext();
	});

	this->add_function("then", [this](const treeml::forest_ext& args){
		ASSERT(!args.empty()) // if there are no arguments, then it is not a function call

		if(!this->if_flag_stack.back()){
			return treeml::forest_ext();
		}

		this->if_flag_stack.push_back(false);
		utki::scope_exit if_flag_scope_exit([this](){
			this->if_flag_stack.pop_back();
		});

		return this->eval(args);
	});

	this->add_function("else", [this](const treeml::forest_ext& args){
		ASSERT(!args.empty()) // if there are no arguments, then it is not a function call

		if(this->if_flag_stack.back()){
			return treeml::forest_ext();
		}

		this->if_flag_stack.push_back(false);
		utki::scope_exit if_flag_scope_exit([this](){
			this->if_flag_stack.pop_back();
		});

		return this->eval(args);
	});

	this->add_function("map", [this](const treeml::forest_ext& args){
		ASSERT(!args.empty()) // if there are no arguments, then it is not a function call

		treeml::forest_ext ret;

		for(const auto& a : args){
			ret.push_back(treeml::tree_ext(a.value, this->eval(a.children)));
		}

		return ret;
	});

	this->add_function("include", [this](const treeml::forest_ext& args){
		ASSERT(!args.empty()) // if there are no arguments, then it is not a function call

		if(!this->file){
			throw exception("include is not supported");
		}

		this->file->set_path(args.front().value.to_string());

		this->file_name_stack.push_back(this->file->path());
		utki::scope_exit file_name_stack_scope_exit([this](){
			this->file_name_stack.pop_back();
		});

		return this->eval(
				treeml::read_ext(*this->file),
				true
			);
	});
}

treeml::forest_ext interpreter::eval(treeml::forest_ext::const_iterator begin, treeml::forest_ext::const_iterator end, bool preserve_vars){
	treeml::forest_ext ret;

	utki::scope_exit context_stack_scope_exit([this, context_stack_size = this->context_stack.size(), preserve_vars](){
		if(!preserve_vars){
			this->context_stack.resize(context_stack_size);
		}
	});

	for(auto i = begin; i != end; ++i){
		try{
			if(i->children.empty()){
				ret.push_back(*i);
				continue;
			}

			treeml::forest_ext output;

			// search for macro
			auto v = this->context_stack.back().find(i->value.to_string());
			if(v.value){
				auto args = this->eval(i->children);

				auto& ctx = this->push_context(&v.ctx);
				utki::scope_exit macro_context_scope_exit([this](){
					this->context_stack.pop_back();
				});

				try{
					ctx.add("@", std::move(args));
				}catch(exception&){
					ASSERT(false)
				}

				output = this->eval(*v.value);
			}else{
				// search for function

				auto func_i = this->functions.find(i->value.to_string());
				if(func_i == this->functions.end()){
					throw exception(std::string("function/macro '") + i->value.to_string() + "' not found");
				}
				output = func_i->second(i->children);
			}

			ret.insert(
					ret.end(),
					std::make_move_iterator(output.begin()),
					std::make_move_iterator(output.end())
				);
		}catch(exception& e){
			throw exception(e.what(), this->file_name_stack.back(), i->value);
		}
	}

	return ret;
}