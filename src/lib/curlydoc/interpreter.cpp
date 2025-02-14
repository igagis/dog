/*
curlydoc - document markup language translator

Copyright (C) 2021 Ivan Gagis <igagis@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/* ================ LICENSE END ================ */

#include "interpreter.hpp"

#include <utki/util.hpp>

using namespace curlydoc;

interpreter::exception::exception(const std::string &message)
    : std::invalid_argument(message + " at:") {}

interpreter::exception::exception(const std::string &message,
                                  const std::string &file,
                                  const tml::leaf_ext &leaf)
    : std::invalid_argument([&]() {
        const auto &l = leaf.info.location;
        std::stringstream ss;
        ss << message << '\n';
        ss << "    " << file << ":" << l.line << ":" << l.offset << ": "
           << leaf.string;
        return ss.str();
      }()) {}

void interpreter::add_function(const std::string &name, function_type &&func) {
  auto res = this->functions.insert(std::make_pair(name, std::move(func)));
  if (!res.second) {
    std::stringstream ss;
    ss << "function '" << name << "' is already added";
    throw std::logic_error(ss.str());
  }
}

void interpreter::add_repeater_function(const std::string &name) {
  this->add_function(name, [this, name](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    tml::forest_ext ret;
    ret.emplace_back(name);

    ret.front().children = this->eval(args);

    return ret;
  });
}

void interpreter::add_repeater_functions(utki::span<const std::string> names) {
  for (const auto &n : names) {
    this->add_repeater_function(n);
  }
}

void interpreter::context::add(const std::string &name,
                               tml::forest_ext &&value) {
  auto i = this->defs.insert(std::make_pair(name, std::move(value)));
  if (!i.second) {
    throw exception("variable name already exists in this context");
  }
}

interpreter::context::find_result
interpreter::context::try_find(const std::string &name) const {
  auto i = this->defs.find(name);
  if (i == this->defs.end()) {
    if (this->prev) {
      return this->prev->try_find(name);
    }
    return {nullptr, *this};
  }
  ASSERT(this->prev)
  return {&i->second, *this->prev};
}

const tml::forest_ext &
interpreter::context::find(const std::string &name) const {
  auto v = this->try_find(name);

  if (!v.value) {
    throw exception(std::string("variable '") + name + "' not found");
  }

  return *v.value;
}

interpreter::context &interpreter::push_context(const context *prev) {
  if (!prev) {
    prev = &this->context_stack.back();
  }
  this->context_stack.emplace_back(prev);
  return this->context_stack.back();
}

interpreter::interpreter(std::unique_ptr<papki::file> file)
    : file_name_stack{"unknown"}, file(std::move(file)) {
  this->add_function("asis", [](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    return args;
  });

  this->add_function("map", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    tml::forest_ext ret;

    for (const auto &a : args) {
      ret.emplace_back(a.value, this->eval(a.children));
    }

    return ret;
  });

  this->add_function("prm", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    tml::forest_ext ret;
    ret.emplace_back("prm");

    for (const auto &a : args) {
      ret.back().children.emplace_back(a.value, this->eval(a.children));
    }

    return ret;
  });

  this->add_function("defs", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    auto &ctx = this->push_context();

    for (const auto &c : args) {
      try {
        ctx.add(c.value.string, this->eval(c.children));
      } catch (exception &e) {
        throw exception(e.what(), this->file_name_stack.back(), c.value);
      }
    }
    return tml::forest_ext();
  });

  this->add_function("$", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    ASSERT(!this->context_stack.empty())

    auto res = this->eval(args);

    if (res.empty()) {
      throw exception("variable name is not given");
    }
    if (res.size() != 1) {
      throw exception("more than one variable name given");
    }

    const auto &name = res.front().value.string;

    const auto &val = this->context_stack.back().find(name);

    return val;
  });

  this->add_function("for", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    auto iter_name = args[0].value.string;
    auto iter_values = this->eval(args[0].children);

    tml::forest_ext ret;

    for (const auto &i : iter_values) {
      auto &ctx = this->push_context();
      utki::scope_exit context_scope_exit(
          [this]() { this->context_stack.pop_back(); });

      try {
        ctx.add(iter_name, {i});
        // do not add in case name already exists
        // NOLINTNEXTLINE(bugprone-empty-catch)
      } catch (exception &) {
        ASSERT(false)
      }

      auto output = this->eval(std::next(args.begin()), args.end());

      ret.insert(ret.end(), std::make_move_iterator(output.begin()),
                 std::make_move_iterator(output.end()));
    }

    return ret;
  });

  this->add_function("if", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    bool flag = [this, &args]() {
      if_flag_push if_flag_push(*this);
      return !this->eval(args).empty();
    }();

    auto &bs = this->if_flag_stack.back();
    bs.flag = flag;
    bs.true_before_or = false;

    return tml::forest_ext();
  });

  this->add_function("then", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    auto &bs = this->if_flag_stack.back();

    if (!bs.flag) {
      return tml::forest_ext();
    }

    if_flag_push if_flag_push(*this);

    return this->eval(args);
  });

  this->add_function("else", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    auto &bs = this->if_flag_stack.back();

    if (bs.flag) {
      return tml::forest_ext();
    }

    if_flag_push if_flag_push(*this);

    return this->eval(args);
  });

  this->add_function("and", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    {
      auto &bs = this->if_flag_stack.back();

      if (!bs.flag || bs.true_before_or) {
        return tml::forest_ext();
      }
    }

    bool flag = [this, &args]() {
      if_flag_push if_flag_push(*this);
      return !this->eval(args).empty();
    }();

    this->if_flag_stack.back().flag = flag;

    return tml::forest_ext();
  });

  this->add_function("or", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    {
      auto &bs = this->if_flag_stack.back();

      if (bs.flag) {
        bs.true_before_or = true;
        return tml::forest_ext();
      }
    }

    bool flag = [this, &args]() {
      if_flag_push if_flag_push(*this);
      return !this->eval(args).empty();
    }();

    this->if_flag_stack.back().flag = flag;

    return tml::forest_ext();
  });

  this->add_function("not", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    if (this->eval(args).empty()) {
      return tml::forest_ext{{"true"}};
    }

    return tml::forest_ext();
  });

  this->add_function("eq", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    auto res = this->eval(args);

    if (res.size() != 2) {
      throw exception("'eq' function requires exactly 2 arguments");
    }

    if (res.front() == res.back()) {
      return tml::forest_ext{{"true"}};
    }
    return tml::forest_ext();
  });

  this->add_function("gt", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    auto res = this->eval(args);

    if (res.size() != 2) {
      throw exception("'eq' function requires exactly 2 arguments");
    }

    if (res.front().value.to_int64() > res.back().value.to_int64()) {
      return tml::forest_ext{{"true"}};
    }
    return tml::forest_ext();
  });

  this->add_function("include", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    if (!this->file) {
      throw exception("include is not supported");
    }

    this->file->set_path(args.front().value.string);

    this->file_name_stack.push_back(this->file->path());
    utki::scope_exit file_name_stack_scope_exit(
        [this]() { this->file_name_stack.pop_back(); });

    return this->eval(tml::read_ext(*this->file), true);
  });

  this->add_function("size", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    // TODO: optimize for the case of size{${v}}, since variables are stored
    // evaluated, no need to copy variable contents via eval()

    auto res = this->eval(args);

    return tml::forest_ext{{std::to_string(res.size())}};
  });

  this->add_function("at", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    // TODO: optimize for the case of at{some_expr ${v}}, since variables are
    // stored evaluated, no need to copy variable contents via eval()

    auto res = this->eval(args);

    if (res.empty()) {
      throw exception("no index argument is given to 'at' function");
    }

    int64_t index = res.front().value.to_int64();

    int64_t size = int64_t(res.size()) - 1;

    if (index < 0) {
      index = size + index;
    }

    if (index < 0 || size <= index) {
      std::stringstream ss;
      ss << "array index (" << index << ") out of bounds (" << size << ")";
      throw exception(ss.str());
    }

    return tml::forest_ext{res[index + 1]};
  });

  this->add_function("get", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    // TODO: optimize for the case of get{some_expr ${v}}, since variables are
    // stored evaluated, no need to copy variable contents via eval()

    auto res = this->eval(args);

    if (res.empty()) {
      throw exception("no key argument is given to 'get' function");
    }

    const auto &key = res.front().value.string;

    for (auto i = std::next(res.begin()); i != res.end(); ++i) {
      if (i->value == key) {
        return i->children;
      }
    }

    std::stringstream ss;
    ss << "key (" << key << ") not found";
    throw exception(ss.str());
  });

  this->add_function("slice", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    // TODO: optimize for the case of slice{some_expr some_expr ${v}}, since
    // variables are stored evaluated, no need to copy variable contents via
    // eval()

    auto evaled = this->eval(args);

    if (evaled.size() < 2) {
      throw exception(
          "too few arguments given to 'slice' function, expected at least 2");
    }

    int64_t size = int64_t(evaled.size()) - 2;

    int64_t begin = evaled.front().value.to_int64();

    int64_t end = [&evaled, &size]() {
      const auto &v = std::next(evaled.begin())->value;
      if (v == "end") {
        return size;
      } else {
        return v.to_int64();
      }
    }();

    if (begin < 0) {
      begin = size + begin;
    }
    if (end < 0) {
      end = size + end;
    }

    if (begin < 0 || size <= begin) {
      std::stringstream ss;
      ss << "begin index (" << begin << ") out of bounds (" << size << ")";
      throw exception(ss.str());
    }
    if (end < 0 || size < end) {
      std::stringstream ss;
      ss << "end index (" << end << ") out of bounds (" << size << ")";
      throw exception(ss.str());
    }

    tml::forest_ext ret;

    std::copy(std::next(evaled.begin(), begin + 2),
              std::next(evaled.begin(), end + 2), std::back_inserter(ret));

    return ret;
  });

  this->add_function("is_word", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    auto res = this->eval(args);

    if (res.size() == 1 && res.front().children.empty()) {
      return tml::forest_ext{{"true"}};
    }
    return tml::forest_ext();
  });

  this->add_function("val", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    tml::forest_ext ret;

    auto evaled = this->eval(args);

    if (evaled.size() == 1) {
      ret.emplace_back(evaled.front().value);
    } else if (!evaled.empty()) {
      throw exception("more than one value passed to 'val' function");
    }

    return ret;
  });

  this->add_function("children", [this](const tml::forest_ext &args) {
    ASSERT(!args.empty()) // if there are no arguments, then it is not a
                          // function call

    auto evaled = this->eval(args);

    if (evaled.size() == 1) {
      return evaled.front().children;
    } else if (!evaled.empty()) {
      throw exception("more than one value passed to 'args' function");
    }

    return tml::forest_ext();
  });

  this->init_std_lib();
}

tml::forest_ext interpreter::eval(tml::forest_ext::const_iterator begin,
                                  tml::forest_ext::const_iterator end,
                                  bool preserve_vars) {
  tml::forest_ext ret;

  utki::scope_exit context_stack_scope_exit(
      [this, context_stack_size = this->context_stack.size(), preserve_vars]() {
        if (!preserve_vars) {
          this->context_stack.resize(context_stack_size);
        }
      });

  for (auto i = begin; i != end; ++i) {
    try {
      if (i->children.empty()) {
        ret.push_back(*i);
        continue;
      }

      tml::forest_ext output;

      // search for macro
      auto v = this->context_stack.back().try_find(i->value.string);
      if (v.value) {
        auto args = this->eval(i->children);

        auto &ctx = this->push_context(&v.ctx);
        utki::scope_exit macro_context_scope_exit(
            [this]() { this->context_stack.pop_back(); });

        try {
          ctx.add("@", std::move(args));
          // do not add in case name already exists
          // NOLINTNEXTLINE(bugprone-empty-catch)
        } catch (exception &) {
          ASSERT(false)
        }

        output = this->eval(*v.value);
      } else {
        // search for function

        auto func_i = this->functions.find(i->value.string);
        if (func_i == this->functions.end()) {
          throw exception(std::string("function/macro '") + i->value.string +
                          "' not found");
        }

        ASSERT(func_i->second)

        output = func_i->second(i->children);

        if (!output.empty()) {
          output.front().value.info.flags.set(
              tml::flag::space, i->value.info.flags.get(tml::flag::space));
        }
      }

      ret.insert(ret.end(), std::make_move_iterator(output.begin()),
                 std::make_move_iterator(output.end()));
    } catch (exception &e) {
      throw exception(e.what(), this->file_name_stack.back(), i->value);
    }
  }

  return ret;
}

tml::forest_ext interpreter::eval() {
  if (!this->file) {
    throw std::logic_error("no file interface provided");
  }

  auto forest = tml::read_ext(*this->file);

  this->file_name_stack.push_back(this->file->path());
  utki::scope_exit file_name_stack_scope_exit(
      [this]() { this->file_name_stack.pop_back(); });

  return this->eval(forest);
}

void interpreter::init_std_lib() {
  const auto forest = tml::read_ext(R"qwertyuiop(
		defs{
			// check if the first element is a parameters element
			is_prm{asis{
				if{ gt{ size{${@}} 0 } } // size > 0
						and{ eq{ val{ at{0 ${@}} } prm} } // @[0].value == prm
						and{ gt{ size{ args{ at{0 ${@}} }} 0} } // @[0].children.size > 0
						then{
							true
						}
			}}
		}
		defs{
			// get parameters value
			get_prm{asis{
				if{ is_prm{${@}} } // if there are parameters
				then{
					args{at{0 ${@}}}
				}
			}}

			// strip out parameters
			no_prm{asis{
				if{ is_prm{${@}} } // if there are parameters
				then{
					slice{1 end ${@}}
				}else{
					${@}
				}
			}}

			// define parameters if non-empty
			prm?{asis{
				if{${@}}then{map{prm{ ${@} }}}
			}}
		}
	)qwertyuiop");

  this->eval(forest, true);
}
