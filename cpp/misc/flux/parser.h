//=====================================================================
//
// parser.h -
//
// Created by liubang on 2023/11/04 17:26
// Last Modified: 2023/11/04 17:26
//
//=====================================================================
#pragma once

#include <algorithm>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

#include "ast.h"
#include "scanner.h"
#include "strconv.h"
#include "token.h"

namespace pl {

struct TokenError {
    TokenError() = default;
    TokenError(std::unique_ptr<Token> t) : token(std::move(t)) {}
    std::unique_ptr<Token> token;
};

class Parser {
public:
    Parser(const std::string& input)
        : scanner_(std::make_unique<Scanner>(input.data(), input.size())), source_(input) {}

    // Parses a file of Flux source code, returning a Package
    std::unique_ptr<Package> parse_single_package(const std::string& pkgpath,
                                                  const std::string& fname) {
        std::shared_ptr<File> ast_file = parse_file(fname);
        auto package = std::make_unique<Package>();
        package->package = ast_file->package->name->name;
        package->base = ast_file->base;
        package->path = pkgpath;
        package->files.emplace_back(ast_file);
        return package;
    }

    // Parses a file of Flux source code, returning a File
    std::unique_ptr<File> parse_file(const std::string& fname) {
        fname_ = fname;
        auto start_pos = peek()->start_pos;
        auto end = Position::invalid();
        auto inner_attributes = parse_attribute_inner_list();
        auto pkg = parse_package_clause(&inner_attributes);
        if (pkg) {
            end = pkg->base->location.end;
        }
        auto imports = parse_import_list(&inner_attributes);
        if (!imports.empty()) {
            end = imports.rbegin()->get()->base->location.end;
        }
        auto body = parse_statement_list(&inner_attributes);
        if (!inner_attributes.empty()) {
            // We have left over attributes from the beginning of the file.
            auto badstmt = std::make_shared<BadStmt>();
            badstmt->base = base_node_from_others(inner_attributes[0]->base.get(),
                                                  inner_attributes.rbegin()->get()->base.get());
            badstmt->text = "extra attributes not associated with anything";
            std::shared_ptr<Statement> stmt = std::make_shared<Statement>();
            stmt->type = Statement::Type::BadStatement;
            stmt->stmt = std::move(badstmt);
            body.emplace_back(std::move(stmt));
        }
        if (!body.empty()) {
            end = body.rbegin()->get()->base()->location.end;
        }
        auto eof = peek()->comments;
        auto ret = std::make_unique<File>();
        ret->base = std::make_shared<BaseNode>();
        ret->base->location = source_location(start_pos, end);
        ret->name = fname_;
        ret->metadata = METADATA;
        ret->package = std::move(pkg);
        ret->body = body;
        ret->imports = imports;
        ret->eof = eof;
        return ret;
    }

private:
    constexpr static char METADATA[] = "parser-type=rust";

    // scan will read the next token from the Scanner. If peek has been used,
    // this will return the peeked token and consume it.
    std::unique_ptr<Token> scan() {
        if (token_) {
            return std::move(token_);
        }
        return scanner_->scan();
    }

    // peek will read the next token from the Scanner and then buffer it.
    // It will return information about the token.
    const Token* peek() {
        if (token_) {
            return token_.get();
        }
        token_ = scanner_->scan();
        return token_.get();
    }

    // peek_with_regex is the same as peek, except that the scan step will allow scanning regexp
    // tokens.
    const Token* peek_with_regex() {
        if (token_ && token_->tok == TokenType::Div) {
            scanner_->set_comments(token_->comments);
            token_.reset();
            scanner_->unread();
        }
        if (!token_) {
            token_ = scanner_->scan_with_regex();
        }
        return token_.get();
    }

    // consume will consume a token that has been retrieve using peek.
    // This will panic if a token has not been buffered with peek.
    std::unique_ptr<Token> consume() {
        if (token_) {
            return std::move(token_);
        }
        // TODO: handler error
        return nullptr;
    }

    // expect will check if the next token is `exp` and error if it is not in either case the token
    // is consumed and returned
    std::unique_ptr<Token> expect(TokenType exp) { return expect_one_of(std::set<TokenType>{exp}); }
    std::unique_ptr<Token> expect_one_of(const std::set<TokenType>& exp) {
        auto t = scan();
        if (exp.count(t->tok) > 0) {
            return t;
        }

        auto exp_to_string = [&exp]() -> std::string {
            auto len = exp.size();
            switch (len) {
            case 0:
                return "";
            case 1:
                return token_to_string(*(exp.begin()));
            default:
                bool first = true;
                std::stringstream ss;
                for (auto tt : exp) {
                    if (!first) {
                        ss << " or ";
                    } else {
                        first = false;
                    }
                    ss << token_to_string(tt);
                }
                return ss.str();
            }
        };

        if (t->tok == TokenType::Eof) {
            errs_.emplace_back("expected " + exp_to_string() + ", got EOF");
        } else {
            std::stringstream ss;
            ss << "expected " << exp_to_string() << ", got " << token_to_string(t->tok) << "("
               << t->lit << ") at " << t->start_pos;
            errs_.emplace_back(ss.str());
        }
        return t;
    }
    // If `exp` is not the next token this will record an error and continue without consuming the
    // token so that the next step in the parse may use it
    std::unique_ptr<Token> expect_or_skip(TokenType exp) {
        auto t = scan();
        if (t->tok == exp) {
            return t;
        }
        token_ = std::move(t);
        auto ret = std::make_unique<Token>();
        ret->start_offset = token_->start_offset;
        ret->end_offset = token_->end_offset;
        ret->start_pos = token_->start_pos;
        ret->end_pos = token_->end_pos;
        if (t->tok == TokenType::Eof) {
            errs_.emplace_back("expected " + token_to_string(exp) + ", got EOF");
            ret->tok = token_->tok;
            ret->comments = token_->comments;
        } else {
            std::stringstream ss;
            ss << "expected " << token_to_string(exp) << ", got " << token_to_string(t->tok) << "("
               << t->lit << ") at " << t->start_pos;
            errs_.emplace_back(ss.str());
            ret->tok = TokenType::Illegal;
        }
        return ret;
    }

    // open will open a new block. It will expect that the next token is the sater token and mark
    // that we expect the end token in the future.
    std::unique_ptr<Token> open(TokenType start, TokenType end) {
        auto t = expect(start);
        if (blocks_.find(end) != blocks_.end()) {
            blocks_[end] = blocks_[end] + 1;
        } else {
            blocks_.insert(std::make_pair(end, 1));
        }
        return t;
    }

    // more will check if we should continue reading tokens for the current block. This is true when
    // the next token is not EOF and the next token is also not one that would close a block.
    bool more() {
        auto t_tok = peek()->tok;
        if (t_tok == TokenType::Eof) {
            return false;
        }
        return blocks_.find(t_tok) == blocks_.end() || blocks_[t_tok] == 0;
    }

    // close will close a block that was opened using open.
    //
    // This function will always decrement the block count for the end token.
    //
    // If the next token is the end token, then this will consume the token and return the pos and
    // lit for the token. Otherwise, it will return NoPos.
    std::unique_ptr<Token> close(TokenType end) {
        if (end == TokenType::Eof) {
            return scan();
        }
        if (blocks_.find(end) == blocks_.end()) {
            // TODO: error handler
            return nullptr;
        }
        blocks_[end] = blocks_[end] - 1;
        const auto* token = peek();
        if (token->tok == end) {
            return consume();
        }
        errs_.emplace_back("expected " + token_to_string(end) + ", got " +
                           token_to_string(token->tok));
        auto ret = std::make_unique<Token>();
        ret->tok = token->tok;
        ret->lit = token->lit;
        ret->start_pos = token->start_pos;
        ret->end_pos = token->end_pos;
        ret->start_offset = token->start_offset;
        ret->end_offset = token->end_offset;
        return ret;
    }

    std::unique_ptr<BaseNode> base_node(SourceLocation location) {
        auto ret = std::make_unique<BaseNode>();
        ret->location = std::move(location);
        ret->errors = errs_;
        return ret;
    }

    std::unique_ptr<BaseNode> base_node_from_token(const Token* token) {
        auto base = base_node_from_tokens(token, token);
        base->comments = token->comments;
        return base;
    }

    std::unique_ptr<BaseNode> base_node_from_tokens(const Token* start, const Token* end) {
        return base_node(source_location(start->start_pos, end->end_pos));
    }

    std::unique_ptr<BaseNode> base_node_from_other_start(const BaseNode* start, const Token* end) {
        return base_node(source_location(start->location.start, end->end_pos));
    }

    std::unique_ptr<BaseNode> base_node_from_other_end(const Token* start, const BaseNode* end) {
        return base_node(source_location(start->start_pos, end->location.end));
    }

    std::unique_ptr<BaseNode> base_node_from_other_end_c(const Token* start,
                                                         const BaseNode* end,
                                                         const Token* comments_from) {
        auto base = base_node(source_location(start->start_pos, end->location.end));
        base->comments = comments_from->comments;
        return base;
    }

    std::unique_ptr<BaseNode>
    base_node_from_other_end_c_a(const Token* start,
                                 const BaseNode* end,
                                 const Token* comments_from,
                                 const std::vector<std::shared_ptr<Attribute>>& attributes) {
        auto base = base_node(source_location(start->start_pos, end->location.end));
        base->comments = comments_from->comments;
        base->attributes = attributes;
        return base;
    }

    std::unique_ptr<BaseNode> base_node_from_others_c(const BaseNode* start,
                                                      const BaseNode* end,
                                                      const Token* comments_from) {
        auto base = base_node_from_pos(start->location.start, end->location.end);
        base->comments = comments_from->comments;
        return base;
    }

    std::unique_ptr<BaseNode> base_node_from_others(const BaseNode* start, const BaseNode* end) {
        return base_node_from_pos(start->location.start, end->location.end);
    }

    std::unique_ptr<BaseNode> base_node_from_pos(const Position& start, const Position& end) {
        return base_node(source_location(start, end));
    }

    SourceLocation source_location(const Position& start, const Position& end) {
        if (!start.is_valid() || !end.is_valid()) {
            return SourceLocation::_default();
        }
        SourceLocation ret;
        ret.file = fname_;
        ret.start = start;
        ret.end = end;
        auto s = scanner_->offset(start);
        auto e = scanner_->offset(end);
        ret.source = std::string(source_.data() + s, (e - s));
        return ret;
    }

    std::vector<std::shared_ptr<Attribute>> parse_attribute_inner_list() {
        auto attributes = std::vector<std::shared_ptr<Attribute>>();
        while (peek()->tok == TokenType::Attribute) {
            attributes.emplace_back(parse_attribute_inner());
        }
        return attributes;
    }

    std::unique_ptr<Attribute> parse_attribute_inner() {
        auto tok = expect(TokenType::Attribute);
        auto lit = tok->lit;
        auto name = std::string(std::find_if(lit.begin(), lit.end(),
                                             [](char c) {
                                                 return c != '@';
                                             }),
                                lit.end());
        return parse_attribute_rest(std::move(tok), name);
    }

    std::unique_ptr<Attribute> parse_attribute_rest(std::unique_ptr<Token> tok,
                                                    const std::string& name) {
        // Parenthesis are optional. No parenthesis means no parameters.
        if (peek()->tok != TokenType::LParen) {
            auto ret = std::make_unique<Attribute>();
            ret->base = base_node_from_token(tok.get());
            ret->name = name;
            return ret;
        }

        open(TokenType::LParen, TokenType::RParen);
        auto params = parse_attribute_params();
        auto end = close(TokenType::RParen);
        auto base = base_node_from_tokens(tok.get(), end.get());
        base->comments = tok->comments;
        auto ret = std::make_unique<Attribute>();
        ret->base = std::move(base);
        ret->name = name;
        ret->params = std::move(params);
        return ret;
    }

    std::vector<std::shared_ptr<AttributeParam>> parse_attribute_params() {
        std::vector<std::shared_ptr<AttributeParam>> params;
        while (more()) {
            auto value = parse_primary_expression();
            auto start_pos = value->base()->location.start;
            auto end_pos = value->base()->location.end;
            std::vector<std::shared_ptr<Comment>> comments;

            if (more()) {
                const auto* t = peek();
                if (t->tok != TokenType::Comma) {
                    errs_.emplace_back("expected comma in attribute parameter list, got " +
                                       token_to_string(t->tok));
                } else {
                    auto tt = consume();
                    end_pos = tt->end_pos;
                    comments = tt->comments;
                }
            }

            auto param = std::make_shared<AttributeParam>();
            param->base = base_node_from_pos(start_pos, end_pos);
            param->value = std::move(value);
            param->comma = comments;
            params.emplace_back(param);
        }
        return params;
    }

    std::unique_ptr<Expression> parse_primary_expression() {
        const auto* t = peek_with_regex();
        auto ret = std::make_unique<Expression>();
        switch (t->tok) {
        case TokenType::Ident: {
            ret->type = Expression::Type::Identifier;
            ret->expr = parse_identifier();
            break;
        }
        case TokenType::Int: {
            ret->type = Expression::Type::IntegerLit;
            ret->expr = parse_int_literal();
            break;
        }
        case TokenType::Float: {
            TokenError err;
            std::unique_ptr<FloatLit> fl;
            std::tie(fl, err) = parse_float_literal();
            if (fl) {
                ret->type = Expression::Type::FloatLit;
                ret->expr = std::move(fl);
            } else {
                return create_bad_expression(std::move(err.token));
            }
            break;
        }
        case TokenType::String: {
            ret->type = Expression::Type::StringLit;
            ret->expr = parse_string_literal();
            break;
        }
        case TokenType::Quote: {
            std::unique_ptr<StringExpr> str_expr;
            TokenError err;
            std::tie(str_expr, err) = parse_string_expression();
            if (str_expr) {
                ret->type = Expression::Type::StringExpr;
                ret->expr = std::move(str_expr);
            }
            break;
        }
        case TokenType::Regex:
            ret->type = Expression::Type::RegexpLit;
            ret->expr = parse_regexp_literral();
            break;
        case TokenType::Time: {
            std::unique_ptr<DateTimeLit> lit;
            TokenError err;
            std::tie(lit, err) = parse_time_literal();
            if (!lit) {
                return create_bad_expression_with_text(
                    std::move(err.token), "invalid data time literal, missing time offset");
            }
            ret->type = Expression::Type::DateTimeLit;
            ret->expr = std::move(lit);
            break;
        }
        case TokenType::Duration: {
            std::unique_ptr<DurationLit> lit;
            TokenError err;
            std::tie(lit, err) = parse_duration_literal();
            if (!lit) {
                return create_bad_expression(std::move(err.token));
            }
            ret->type = Expression::Type::DurationLit;
            ret->expr = std::move(lit);
            break;
        }
        case TokenType::PipeReceive:
            ret->type = Expression::Type::PipeLit;
            ret->expr = parse_pipe_literal();
            break;
        case TokenType::LBrack: {
            auto start = open(TokenType::LBrack, TokenType::RBrack);
            return parse_array_or_dict(std::move(start));
        }
        case TokenType::LBrace:
            ret->type = Expression::Type::ObjectExpr;
            ret->expr = parse_object_literal();
            break;
        case TokenType::LParen:
            return parse_paren_expression();
        case TokenType::Dot:
            ret->type = Expression::Type::LabelLit;
            ret->expr = parse_label_literal();
            break;
        default:
            break;
        }
        return ret;
    }

    std::unique_ptr<Block> parse_block() {
        auto start = open(TokenType::LBrace, TokenType::RBrace);
        auto stmts = parse_statement_list({});
        auto end = close(TokenType::RBrace);
        return std::make_unique<Block>(base_node_from_tokens(start.get(), end.get()),
                                       start->comments, stmts, end->comments);
    }

    std::unique_ptr<Expression>
    parse_function_body_expression(std::unique_ptr<Token> lparen,
                                   std::unique_ptr<Token> rparen,
                                   std::unique_ptr<Token> arrow,
                                   const std::vector<std::shared_ptr<Property>>& params) {
        const auto* t = peek();
        if (t->tok == TokenType::LBrace) {
            auto block = parse_block();
            auto expr = std::make_unique<Expression>(Expression::Type::FunctionExpr);
            auto base = base_node_from_other_end(lparen.get(), block->base.get());
            auto fbody = std::make_unique<FunctionBody>(FunctionBody::Type::Block);
            fbody->body = std::move(block);
            auto func =
                std::make_shared<FunctionExpr>(std::move(base), lparen->comments, params,
                                               rparen->comments, arrow->comments, std::move(fbody));
            expr->expr = std::move(func);
            return expr;
        }
        auto expr = parse_expression();
        auto ret = std::make_unique<Expression>(Expression::Type::FunctionExpr);
        auto base = base_node_from_other_end(lparen.get(), expr->base().get());
        auto fbody = std::make_unique<FunctionBody>(FunctionBody::Type::Expression);
        fbody->body = std::move(expr);
        auto func =
            std::make_shared<FunctionExpr>(std::move(base), lparen->comments, params,
                                           rparen->comments, arrow->comments, std::move(fbody));
        ret->expr = std::move(func);
        return ret;
    }

    std::unique_ptr<Expression>
    parse_function_expression(std::unique_ptr<Token> lparen,
                              std::unique_ptr<Token> rparen,
                              const std::vector<std::shared_ptr<Property>>& params) {
        auto arrow = expect_or_skip(TokenType::Arrow);
        return parse_function_body_expression(std::move(lparen), std::move(rparen),
                                              std::move(arrow), params);
    }

    // TODO
    std::unique_ptr<Expression> parse_paren_ident_expression(std::unique_ptr<Token> lparen,
                                                             std::unique_ptr<Identifier> key) {}

    std::unique_ptr<Expression> parse_paren_body_expression(std::unique_ptr<Token> lparen) {
        const auto* t = peek();
        if (t->tok == TokenType::RParen) {
            auto tt = close(TokenType::RParen);
            return parse_function_expression(std::move(lparen), std::move(tt), {});
        }
        if (t->tok == TokenType::Ident) {
            auto ident = parse_identifier();
            return parse_paren_ident_expression(std::move(lparen), std::move(ident));
        }
        auto expr = parse_expression_while_more(nullptr, {});
        if (!expr) {
            expr = std::make_unique<Expression>();
            expr->type = Expression::Type::BadExpr;
            auto bad_expr = std::make_shared<BadExpr>();
            SourceLocation sl(t->start_pos, t->end_pos);
            bad_expr->base = std::make_shared<BaseNode>(sl);
            bad_expr->text = t->lit;
            expr->expr = std::move(bad_expr);
        }
        auto rparen = close(TokenType::RParen);
        auto ret = std::make_unique<Expression>();
        ret->type = Expression::Type::ParenExpr;
        ret->expr =
            std::make_shared<ParenExpr>(base_node_from_tokens(lparen.get(), rparen.get()),
                                        lparen->comments, std::move(expr), rparen->comments);
        return ret;
    }

    std::unique_ptr<Expression> parse_paren_expression() {
        auto lparen = open(TokenType::LParen, TokenType::RParen);
        return parse_paren_body_expression(std::move(lparen));
    }

    // TODO
    std::unique_ptr<LabelLit> parse_label_literal() {}

    std::tuple<std::unique_ptr<StringExpr>, TokenError> parse_string_expression() {
        auto start = expect(TokenType::Quote);
        std::vector<std::shared_ptr<StringExprPart>> parts;
        for (;;) {
            auto t = scanner_->scan_with_expr();
            switch (t->tok) {
            case TokenType::Text: {
                auto value = StrConv::parse_text(t->lit);
                if (!value.ok()) {
                    return {nullptr, TokenError(std::move(t))};
                }
                std::shared_ptr<StringExprPart> p = std::make_shared<StringExprPart>();
                std::shared_ptr<TextPart> tp = std::make_shared<TextPart>();
                tp->base = base_node_from_token(t.get());
                tp->value = value.t();
                p->type = StringExprPart::Type::Text;
                p->part = tp;
                parts.emplace_back(p);
                break;
            }
            case TokenType::StringExpr: {
                auto expr = parse_expression();
                auto end = expect(TokenType::RBrace);
                std::shared_ptr<StringExprPart> p = std::make_shared<StringExprPart>();
                std::shared_ptr<InterpolatedPart> ip = std::make_shared<InterpolatedPart>();
                ip->base = base_node_from_tokens(t.get(), end.get());
                ip->expression = std::move(expr);
                p->type = StringExprPart::Type::Interpolated;
                p->part = ip;
                parts.emplace_back(p);
                break;
            }
            case TokenType::Quote: {
                auto string_expr = std::make_unique<StringExpr>();
                string_expr->base = base_node_from_tokens(start.get(), t.get());
                string_expr->parts = std::move(parts);
                return {std::move(string_expr), TokenError()};
            }
            default: {
                auto loc = source_location(t->start_pos, t->end_pos);
                std::stringstream ss;
                ss << "got unexpcted token in string expression " << loc << ": "
                   << token_to_string(t->tok);
                errs_.emplace_back(ss.str());
                auto string_expr = std::make_unique<StringExpr>();
                string_expr->base = base_node_from_tokens(start.get(), t.get());
                return {std::move(string_expr), TokenError()};
            }
            }
        }
    }

    // TODO
    std::unique_ptr<Expression> parse_expression() { return nullptr; }

    std::unique_ptr<ObjectExpr> parse_object_literal() {
        auto start = open(TokenType::LBrace, TokenType::RBrace);
        auto obj = parse_object_body();
        auto end = close(TokenType::RBrace);
        obj->base = base_node_from_tokens(start.get(), end.get());
        obj->lbrace = std::move(start->comments);
        obj->rbrace = std::move(end->comments);
        return obj;
    }

    std::unique_ptr<ObjectExpr> parse_object_body() {
        const auto* t = peek();
        if (t->tok == TokenType::Ident) {
            auto ident = parse_identifier();
            return parse_object_body_suffix(std::move(ident));
        }
        if (t->tok == TokenType::String) {
            auto s = parse_string_literal();
            auto propk = std::make_unique<PropertyKey>();
            propk->type = PropertyKey::Type::StringLiteral;
            propk->key = std::move(s);
            auto props = parse_property_list_suffix(std::move(propk));
            auto objexpr = std::make_unique<ObjectExpr>();
            objexpr->base = std::make_shared<BaseNode>();
            objexpr->properties = std::move(props);
            return objexpr;
        }
        auto objexpr = std::make_unique<ObjectExpr>();
        objexpr->base = std::make_shared<BaseNode>();
        objexpr->properties = parse_property_list();
        return objexpr;
    }

    std::vector<std::shared_ptr<Property>> parse_property_list() {
        std::vector<std::shared_ptr<Property>> params;
        while (more()) {
            const auto* t = peek();
            std::shared_ptr<Property> p;
            if (t->tok == TokenType::Ident) {
                p = parse_ident_property();
            } else if (t->tok == TokenType::String) {
                p = parse_string_property();
            } else {
                p = parse_invalid_property();
            }
            if (more()) {
                t = peek();
                if (t->tok != TokenType::Comma) {
                    errs_.emplace_back("expected comma in property list, got " +
                                       token_to_string(t->tok));
                } else {
                    auto nt = consume();
                    p->comma = t->comments;
                }
            }
            params.emplace_back(std::move(p));
        }

        return params;
    }

    std::unique_ptr<Property> parse_string_property() {
        auto key = parse_string_literal();
        auto pk = std::unique_ptr<PropertyKey>();
        pk->type = PropertyKey::Type::StringLiteral;
        pk->key = std::move(key);
        return parse_property_suffix(std::move(pk));
    }

    std::unique_ptr<Property> parse_ident_property() {
        auto key = parse_identifier();
        auto pk = std::unique_ptr<PropertyKey>();
        pk->type = PropertyKey::Type::Identifier;
        pk->key = std::move(key);
        return parse_property_suffix(std::move(pk));
    }

    std::unique_ptr<Property> parse_invalid_property() {
        const auto* t = peek();
        std::unique_ptr<Expression> value;
        if (t->tok == TokenType::Colon) {
            errs_.emplace_back("missing property key");
            consume();
            value = parse_property_value();
        } else if (t->tok == TokenType::Comma) {
            errs_.emplace_back("missing property in property list");
        } else {
            errs_.emplace_back("unexpcted token for property key: " + token_to_string(t->tok) +
                               t->lit);

            // We are not really parsing an expression, this is just a way to advance to just before
            // the next comma, colon, end of block, or EOF.
            parse_expression_while_more(nullptr, {TokenType::Comma, TokenType::Colon});

            // If we stopped at a colon, attempt to parse the value
            if (peek()->tok == TokenType::Colon) {
                consume();
                value = parse_property_value();
            }
        }
        auto end_start_pos = peek()->start_pos;
        std::unique_ptr<Property> p;
        std::shared_ptr<PropertyKey> k;
        k->type = PropertyKey::Type::StringLiteral;
        std::shared_ptr<StringLit> sk;
        sk->base = base_node_from_pos(t->start_pos, t->start_pos);
        sk->value = "<invalid>";
        k->key = std::move(sk);

        p->base = base_node_from_pos(t->start_pos, end_start_pos);
        p->value = std::move(value);
        p->key = std::move(k);
        return p;
    }

    std::unique_ptr<Expression>
    parse_expression_while_more(std::unique_ptr<Expression> init,
                                const std::set<TokenType>& stop_tokens) {
        for (;;) {
            const auto* t = peek();
            if (stop_tokens.contains(t->tok) || !more()) {
                break;
            }
            auto e = parse_expression();
            if (e->type == Expression::Type::BadExpr) {
                auto invalid_t = scan();
                auto loc = source_location(invalid_t->start_pos, invalid_t->end_pos);
                std::stringstream ss;
                ss << "invalid expression " << loc << ":" << invalid_t->lit;
                errs_.emplace_back(ss.str());
                continue;
            }
            if (init) {
                std::unique_ptr<Expression> ex = std::make_unique<Expression>();
                ex->type = Expression::Type::BinaryExpr;
                std::shared_ptr<BinaryExpr> be = std::make_unique<BinaryExpr>();
                be->base = base_node_from_others(init->base().get(), e->base().get());
                be->op = Operator::InvalidOperator;
                be->left = std::move(init);
                be->right = std::move(e);
                ex->expr = std::move(be);
                init = std::move(ex);
            } else {
                init = std::move(e);
            }
        }
        return init;
    }

    std::unique_ptr<Expression> parse_property_value() {
        auto res = parse_expression_while_more(nullptr, {TokenType::Comma, TokenType::Colon});
        if (!res) {
            errs_.emplace_back("missing property value");
        }
        return res;
    }

    std::unique_ptr<Property> parse_property_suffix(std::unique_ptr<PropertyKey> key) {
        const auto* t = peek();
        std::unique_ptr<Token> tt;
        std::unique_ptr<Expression> value;
        std::vector<std::shared_ptr<Comment>> sep;
        if (t->tok == TokenType::Colon) {
            tt = consume();
            value = parse_property_value();
            sep = t->comments;
        }
        std::shared_ptr<BaseNode> value_base;
        if (value) {
            value_base = value->base();
        } else {
            value_base = key->base();
        }
        std::unique_ptr<Property> ret = std::make_unique<Property>();
        ret->base = base_node_from_others(key->base().get(), value_base.get());
        ret->key = std::move(key);
        ret->value = std::move(value);
        ret->separator = std::move(sep);
        return ret;
    }

    std::vector<std::shared_ptr<Property>>
    parse_property_list_suffix(std::unique_ptr<PropertyKey> key) {
        std::vector<std::shared_ptr<Property>> props;
        auto p = parse_property_suffix(std::move(key));
        props.emplace_back(std::move(p));
        if (!more()) {
            return props;
        }
        const auto* t = peek();
        if (t->tok == TokenType::Comma) {
            errs_.emplace_back("expected comma in property list, got " + token_to_string(t->tok));
        } else {
            auto last = props.size() - 1;
            auto tt = consume();
            props[last]->comma = t->comments;
        }
        auto list = parse_property_list();
        props.insert(props.end(), std::make_move_iterator(list.begin()),
                     std::make_move_iterator(list.end()));
        return props;
    }

    std::unique_ptr<ObjectExpr> parse_object_body_suffix(std::unique_ptr<Identifier> id) {
        const auto* t = peek();
        std::unique_ptr<ObjectExpr> obj_expr = std::make_unique<ObjectExpr>();
        obj_expr->base = std::make_shared<BaseNode>();
        if (t->tok == TokenType::Ident) {
            if (t->lit != "with") {
                errs_.emplace_back("");
            }
            auto tt = consume();
            auto props = parse_property_list();
            std::shared_ptr<WithSource> with_source = std::make_shared<WithSource>();
            with_source->source = std::move(id);
            with_source->with = t->comments;
            obj_expr->with = std::move(with_source);
            obj_expr->properties = std::move(props);
        } else {
            std::unique_ptr<PropertyKey> ident = std::make_unique<PropertyKey>();
            ident->type = PropertyKey::Type::Identifier;
            ident->key = std::move(id);
            auto props = parse_property_list_suffix(std::move(ident));
            obj_expr->properties = std::move(props);
        }
        return obj_expr;
    }

    std::unique_ptr<Expression> parse_array_or_dict(std::unique_ptr<Token> start) {
        switch (peek()->tok) {
        // empty dictinary [:]
        case TokenType::Colon: {
            consume();
            auto end = close(TokenType::RBrack);
            auto base = base_node_from_tokens(start.get(), end.get());
            auto lbrack = start->comments;
            auto rbrack = end->comments;
            auto dict_expr = std::make_shared<DictExpr>();
            dict_expr->base = std::move(base);
            dict_expr->lbrack = std::move(lbrack);
            dict_expr->rbrack = std::move(rbrack);

            auto expr = std::make_unique<Expression>();
            expr->type = Expression::Type::ArrayExpr;
            expr->expr = std::move(dict_expr);
            return expr;
        }
        // empty array []
        case TokenType::RBrack: {
            auto end = close(TokenType::RBrack);
            auto base = base_node_from_tokens(start.get(), end.get());
            auto lbrack = start->comments;
            auto rbrack = end->comments;
            auto arr_expr = std::make_shared<ArrayExpr>();
            arr_expr->base = std::move(base);
            arr_expr->lbrack = std::move(lbrack);
            arr_expr->rbrack = std::move(rbrack);

            auto expr = std::make_unique<Expression>();
            expr->type = Expression::Type::ArrayExpr;
            expr->expr = std::move(arr_expr);

            return expr;
        }
        default: {
            auto expr = parse_expression();
            if (peek()->tok == TokenType::Colon) {
                // non-empty dictionary
                consume();
                auto val = parse_expression();
                return parse_dict_items_rest(std::move(start), std::move(expr), std::move(val));
            }
            // non-empty array
            return parse_array_items_rest(std::move(start), std::move(expr));
        }
        }
    }

    std::unique_ptr<Expression> parse_array_items_rest(std::unique_ptr<Token> start,
                                                       std::unique_ptr<Expression> init) {
        auto expr = std::make_unique<Expression>();
        expr->type = Expression::Type::ArrayExpr;
        auto arr_expr = std::make_shared<ArrayExpr>();

        if (peek()->tok == TokenType::RBrack) {
            auto end = close(TokenType::RBrack);
            arr_expr->base = base_node_from_tokens(start.get(), end.get());
            arr_expr->lbrack = std::move(start->comments);
            auto arr_item = std::make_shared<ArrayItem>();
            arr_item->expression = std::move(init);
            arr_expr->rbrack = std::move(end->comments);
            arr_expr->elements.push_back(std::move(arr_item));
        } else {
            // else
            auto comma = expect(TokenType::Comma);
            std::vector<std::shared_ptr<ArrayItem>> items;
            auto arr_item = std::make_shared<ArrayItem>();
            arr_item->expression = std::move(init);
            arr_item->comma = std::move(comma->comments);
            items.emplace_back(std::move(arr_item));

            auto last = peek()->start_offset;
            while (more()) {
                std::vector<std::shared_ptr<Comment>> ncomma;
                auto expression = parse_expression();
                if (peek()->tok == TokenType::Comma) {
                    comma = scan();
                    ncomma = comma->comments;
                }
                auto narr_item = std::make_shared<ArrayItem>();
                narr_item->expression = std::move(expression);
                narr_item->comma = std::move(ncomma);
                items.emplace_back(std::move(narr_item));

                auto _this = peek()->start_offset;
                if (last == _this) {
                    break;
                }
                last = _this;
            }
            auto end = close(TokenType::RBrack);
            arr_expr->base = base_node_from_tokens(start.get(), end.get());
            arr_expr->lbrack = std::move(start->comments);
            arr_expr->elements = std::move(items);
            arr_expr->rbrack = std::move(end->comments);
        }

        expr->expr = std::move(arr_expr);
        return expr;
    }

    std::unique_ptr<Expression> parse_dict_items_rest(std::unique_ptr<Token> start,
                                                      std::unique_ptr<Expression> key,
                                                      std::unique_ptr<Expression> val) {
        auto expr = std::make_unique<Expression>();
        expr->type = Expression::Type::DictExpr;
        auto dict_expr = std::make_shared<DictExpr>();

        if (peek()->tok == TokenType::RBrack) {
            auto end = close(TokenType::RBrack);
            dict_expr->base = base_node_from_tokens(start.get(), end.get());
            dict_expr->lbrack = std::move(start->comments);
            dict_expr->rbrack = std::move(end->comments);
            std::shared_ptr<DictItem> item = std::make_shared<DictItem>();
            item->key = std::move(key);
            item->val = std::move(val);
            dict_expr->elements.emplace_back(std::move(item));
        } else {
            auto comma = expect(TokenType::Comma);
            std::vector<std::shared_ptr<DictItem>> items;
            std::shared_ptr<DictItem> item = std::make_shared<DictItem>();
            item->key = std::move(key);
            item->val = std::move(val);
            item->comma = std::move(comma->comments);
            items.emplace_back(std::move(item));

            while (more()) {
                auto nkey = parse_expression();
                expect(TokenType::Colon);
                auto nval = parse_expression();
                std::shared_ptr<DictItem> nitem = std::make_shared<DictItem>();
                nitem->key = std::move(nkey);
                nitem->val = std::move(nval);
                if (peek()->tok == TokenType::Comma) {
                    comma = scan();
                    nitem->comma = std::move(comma->comments);
                }
                items.emplace_back(std::move(nitem));
            }

            auto end = close(TokenType::RBrack);
            dict_expr->base = base_node_from_tokens(start.get(), end.get());
            dict_expr->lbrack = std::move(start->comments);
            dict_expr->rbrack = std::move(end->comments);
            dict_expr->elements = std::move(items);
        }

        expr->expr = std::move(dict_expr);
        return expr;
    }

    std::unique_ptr<PipeLit> parse_pipe_literal() {
        auto t = expect(TokenType::PipeReceive);
        auto pipe_lit = std::make_unique<PipeLit>();
        pipe_lit->base = base_node_from_token(t.get());
        return pipe_lit;
    }

    std::tuple<std::unique_ptr<DurationLit>, TokenError> parse_duration_literal() {
        auto t = expect(TokenType::Duration);
        auto value = StrConv::parse_duration(t->lit);
        if (value.ok()) {
            auto dl = std::make_unique<DurationLit>();
            dl->base = base_node_from_token(t.get());
            dl->values = value.t();
            return {std::move(dl), TokenError()};
        }
        return {nullptr, TokenError()};
    }

    std::tuple<std::unique_ptr<DateTimeLit>, TokenError> parse_time_literal() {
        auto t = expect(TokenType::Time);
        auto value = StrConv::parse_time(t->lit);
        if (value.ok()) {
            auto datetime_lit = std::make_unique<DateTimeLit>();
            datetime_lit->base = base_node_from_token(t.get());
            datetime_lit->value = value.t();
            return {std::move(datetime_lit), TokenError()};
        }
        return {nullptr, TokenError(std::move(t))};
    }

    std::unique_ptr<RegexpLit> parse_regexp_literral() {
        auto t = expect(TokenType::Regex);
        auto value = StrConv::parse_regex(t->lit);
        auto ret = std::make_unique<RegexpLit>();
        ret->base = base_node_from_token(t.get());
        if (!value.ok()) {
            errs_.emplace_back(value.e());
        } else {
            ret->value = value.t();
        }
        return ret;
    }

    std::unique_ptr<Expression> parse_conditional_expression() {
        const auto* t = peek();
        if (t->tok == TokenType::If) {
            auto if_tok = scan();
            auto test = parse_expression();
            auto then_tok = expect_or_skip(TokenType::Then);
            auto cons = then_tok->tok == TokenType::Then
                            ? parse_expression()
                            : create_placeholder_expression(then_tok.get());
            auto else_tok = expect_or_skip(TokenType::Else);
            auto alt = else_tok->tok == TokenType::Else
                           ? parse_expression()
                           : create_placeholder_expression(else_tok.get());

            auto cond_expr = std::make_shared<ConditionalExpr>();
            cond_expr->base = base_node_from_other_end(t, alt->base().get());
            cond_expr->tk_if = if_tok->comments;
            cond_expr->tk_then = then_tok->comments;
            cond_expr->test = std::move(test);
            cond_expr->consequent = std::move(cons);
            cond_expr->tk_else = else_tok->comments;
            cond_expr->alternate = std::move(alt);
            auto exp = std::make_unique<Expression>();
            exp->type = Expression::Type::ConditionalExpr;
            exp->expr = cond_expr;
            return exp;
        }
        return parse_logical_or_expression();
    }

    std::unique_ptr<Expression> create_placeholder_expression(const Token* tok) {
        auto expr = std::make_unique<Expression>();
        auto bad_expr = std::make_shared<BadExpr>();

        bad_expr->base = std::make_shared<BaseNode>();
        bad_expr->base->location = source_location(tok->start_pos, tok->end_pos);

        expr->type = Expression::Type::BadExpr;
        expr->expr = bad_expr;

        return expr;
    }

    std::optional<Operator> parse_logical_unary_operator() {
        const auto* t = peek();
        if (t->tok == TokenType::Not) {
            return Operator::NotOperator;
        }
        if (t->tok == TokenType::Exists) {
            return Operator::ExistsOperator;
        }
        return std::nullopt;
    }

    std::optional<LogicalOperator> parse_and_operator() {
        const auto* t = peek();
        if (t->tok == TokenType::And) {
            return LogicalOperator::AndOperator;
        }
        return std::nullopt;
    }

    // TODO
    std::shared_ptr<Expression>
    parse_logical_and_expression_suffix(const std::shared_ptr<Expression>& expr) {
        std::shared_ptr<Expression> res = expr;
        for (;;) {
            auto and_op = parse_and_operator();
            if (!and_op) {
                break;
            }
            auto t = scan();
            auto rhs = parse_logical_unary_expression();
            auto nexpr = std::make_shared<Expression>();
            nexpr->type = Expression::Type::LogicalExpr;

            auto logicexpr = std::make_shared<LogicalExpr>();
            logicexpr->base =
                base_node_from_others_c(res->base().get(), rhs->base().get(), t.get());
            logicexpr->op = and_op.value();
            logicexpr->left = res;
            logicexpr->right = std::move(rhs);
            nexpr->expr = logicexpr;

            res = nexpr;
        }
        return res;
    }

    // TODO
    std::shared_ptr<Expression> parse_logical_unary_expression() {
        const auto* t = peek();
        auto op = parse_logical_unary_operator();
        if (op) {
            consume();
            auto expr = parse_logical_unary_expression();
            std::unique_ptr<Expression> ret = std::make_unique<Expression>();
            ret->type = Expression::Type::UnaryExpr;
            std::shared_ptr<UnaryExpr> uexpr = std::make_shared<UnaryExpr>();
            uexpr->base = base_node_from_other_end_c(t, expr->base().get(), t);
            uexpr->op = op.value();
            uexpr->argument = std::move(expr);
            ret->expr = std::move(uexpr);
            return ret;
        }
        return parse_comparison_expression();
    }

    std::shared_ptr<Expression> parse_comparison_expression() {
        auto expr = parse_additive_expression();
        return parse_comparison_expression_suffix(expr);
    }

    std::optional<Operator> parse_comparison_operator() {
        const auto* t = peek();
        switch (t->tok) {
        case TokenType::Eq:
            return Operator::EqualOperator;
        case TokenType::Neq:
            return Operator::NotEqualOperator;
        case TokenType::Lte:
            return Operator::LessThanEqualOperator;
        case TokenType::Lt:
            return Operator::LessThanOperator;
        case TokenType::Gte:
            return Operator::GreaterThanEqualOperator;
        case TokenType::Gt:
            return Operator::GreaterThanOperator;
        case TokenType::RegexEq:
            return Operator::RegexpMatchOperator;
        case TokenType::RegexNeq:
            return Operator::NotRegexpMatchOperator;
        default:
            return std::nullopt;
        }
    }

    // TODO
    std::unique_ptr<Expression> parse_multiplicative_expression() { return nullptr; }

    std::shared_ptr<Expression> parse_additive_expression() {
        auto expr = parse_multiplicative_expression();
        return parse_additive_expression_suffix(std::move(expr));
    }

    std::optional<Operator> parse_additive_operator() {
        const auto* t = peek();
        if (t->tok == TokenType::Add) {
            return Operator::AdditionOperator;
        }
        if (t->tok == TokenType::Sub) {
            return Operator::SubtractionOperator;
        }
        return std::nullopt;
    }

    // TODO
    std::shared_ptr<Expression> parse_additive_expression_suffix(std::unique_ptr<Expression> expr) {
        std::shared_ptr<Expression> ret = std::move(expr);
        for (;;) {
            auto op = parse_additive_operator();
            if (!op) {
                break;
            }
            auto t = scan();
            auto rhs = parse_multiplicative_expression();
            auto nret = std::make_unique<Expression>();
            nret->type = Expression::Type::BinaryExpr;
            auto binexpr = std::make_shared<BinaryExpr>();
            binexpr->base = base_node_from_others_c(ret->base().get(), rhs->base().get(), t.get());
            binexpr->left = ret;
            binexpr->right = std::move(rhs);
            nret->expr = std::move(binexpr);
            ret = std::move(nret);
        }
        return ret;
    }

    std::shared_ptr<Expression>
    parse_comparison_expression_suffix(const std::shared_ptr<Expression>& expr) {
        std::shared_ptr<Expression> ret = expr;
        for (;;) {
            auto op = parse_comparison_operator();
            if (!op) {
                break;
            }
            auto t = scan();
            auto rhs = parse_additive_expression();
            auto nret = std::make_unique<Expression>();
            nret->type = Expression::Type::BinaryExpr;
            auto binexpr = std::make_shared<BinaryExpr>();
            binexpr->base = base_node_from_others_c(ret->base().get(), rhs->base().get(), t.get());
            binexpr->left = ret;
            binexpr->right = std::move(rhs);
            nret->expr = std::move(binexpr);
            ret = std::move(nret);
        }
        return ret;
    }

    std::shared_ptr<Expression> parse_logical_and_expression() {
        auto expr = parse_logical_unary_expression();
        return parse_logical_and_expression_suffix(expr);
    }

    // TODO
    std::unique_ptr<Expression>
    parse_logical_or_expression_suffix(const std::shared_ptr<Expression>& expr) {
        return nullptr;
    }

    std::unique_ptr<Expression> parse_logical_or_expression() {
        auto expr = parse_logical_and_expression();
        return parse_logical_or_expression_suffix(expr);
    }

    std::unique_ptr<StringLit> parse_string_literal() {
        auto t = expect(TokenType::String);
        return new_string_literal(std::move(t));
    }

    std::unique_ptr<StringLit> new_string_literal(std::unique_ptr<Token> t) {
        auto result = StrConv::parse_string(t->lit);
        if (!result.ok()) {
            return nullptr;
        }
        auto ret = std::make_unique<StringLit>();
        ret->base = base_node_from_token(t.get());
        ret->value = result.t();
        return ret;
    }

    std::unique_ptr<Expression> create_bad_expression(std::unique_ptr<Token> tok) {
        std::string ss = "invalid token for primary expression: " + token_to_string(tok->tok);
        return create_bad_expression_with_text(std::move(tok), std::move(ss));
    }

    std::unique_ptr<Expression> create_bad_expression_with_text(std::unique_ptr<Token> tok,
                                                                std::string_view text) {
        auto base = std::make_shared<BaseNode>();
        base->location = source_location(tok->start_pos, tok->end_pos);

        auto expr = std::make_shared<BadExpr>();
        expr->text = text;
        expr->base = base;

        auto ret = std::make_unique<Expression>();
        ret->type = Expression::Type::BadExpr;
        ret->expr = expr;

        return ret;
    }

    std::unique_ptr<Identifier> parse_identifier() {
        auto t = expect_or_skip(TokenType::Ident);
        auto ret = std::make_unique<Identifier>();
        ret->base = base_node_from_token(t.get());
        ret->name = t->lit;
        return ret;
    }

    std::unique_ptr<IntegerLit> parse_int_literal() {
        auto t = expect(TokenType::Int);
        auto ret = std::make_unique<IntegerLit>();
        ret->base = base_node_from_token(t.get());
        if (t->lit.starts_with('0') && t->lit.length() > 1) {
            errs_.emplace_back("invalid integer literal " + t->lit +
                               ": nonzero value cannot start with 0");
            ret->value = 0;
            return ret;
        }

        try {
            int64_t value = std::stol(t->lit);
            ret->value = value;
        } catch (...) {
            errs_.emplace_back("invalid integer literal " + t->lit + ": value out of range");
            ret->value = 0;
        }

        return ret;
    }

    std::tuple<std::unique_ptr<FloatLit>, TokenError> parse_float_literal() {
        auto t = expect(TokenType::Float);
        try {
            long double value = std::stod(t->lit);
            auto ret = std::make_unique<FloatLit>();
            ret->base = base_node_from_token(t.get());
            ret->value = value;
            return {std::move(ret), TokenError()};
        } catch (...) {
            TokenError tok_err;
            tok_err.token = std::move(t);
            return {std::unique_ptr<FloatLit>(), std::move(tok_err)};
        }
    }

    // TODO:
    std::unique_ptr<PackageClause>
    parse_package_clause(std::vector<std::shared_ptr<Attribute>>* attributes) {
        return nullptr;
    }

    // TODO:
    std::vector<std::shared_ptr<ImportDeclaration>>
    parse_import_list(std::vector<std::shared_ptr<Attribute>>* attributes) {
        return {};
    }

    // TODO:
    std::vector<std::shared_ptr<Statement>>
    parse_statement_list(std::vector<std::shared_ptr<Attribute>>* attributes) {
        return {};
    }

private:
    std::unique_ptr<Scanner> scanner_;
    std::unique_ptr<Token> token_;
    std::vector<std::string> errs_;
    std::map<TokenType, int32_t> blocks_;
    std::string source_;
    std::string fname_;
    uint32_t depth_{0};
};

} // namespace pl
