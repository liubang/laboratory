#include "parser.h"

#include <cassert>

#include "diagnostic.h"

namespace highkyck {
namespace bfcc {

std::shared_ptr<ProgramNode> Parser::Parse() {
  auto node = std::make_shared<ProgramNode>();
  ids_ = &node->local_ids_;
  while (lexer_ptr_->CurrentToken()->type != TokenType::Eof) {
    node->PushStmt(ParseStmt());
  }
  return node;
}

std::shared_ptr<AstNode> Parser::ParseStmt() {
  if (lexer_ptr_->CurrentToken()->type == TokenType::If) {
    lexer_ptr_->GetNextToken();
    lexer_ptr_->ExpectToken(TokenType::LParent);
    auto c = ParseExpr();
    lexer_ptr_->ExpectToken(TokenType::RParent);
    auto t = ParseStmt();
    std::shared_ptr<AstNode> e;
    if (lexer_ptr_->CurrentToken()->type == TokenType::Else) {
      lexer_ptr_->GetNextToken();
      e = ParseStmt();
    }
    return std::make_shared<IfStmtNode>(c, t, e);
  } else if (lexer_ptr_->CurrentToken()->type == TokenType::LBrace) {
    lexer_ptr_->GetNextToken();
    auto node = std::make_shared<BlockStmtNode>();
    while (lexer_ptr_->CurrentToken()->type != TokenType::RBrace) {
      node->AddStmt(ParseStmt());
    }
    lexer_ptr_->ExpectToken(TokenType::RBrace);
    return node;
  } else {
    auto node = std::make_shared<ExprStmtNode>(ParseExpr());
    assert(lexer_ptr_->CurrentToken()->type == TokenType::Semicolon);
    lexer_ptr_->GetNextToken();
    return node;
  }
}

std::shared_ptr<AstNode> Parser::ParseExpr() {
  return ParseAssignExpr();
}

std::shared_ptr<AstNode> Parser::ParseAssignExpr() {
  auto left = ParseEqualExpr();
  if (lexer_ptr_->CurrentToken()->type == TokenType::Assign) {
    lexer_ptr_->GetNextToken();
    auto node = std::make_shared<AssignExprNode>(left, ParseAssignExpr());
    return node;
  }
  return left;
}

std::shared_ptr<AstNode> Parser::ParseEqualExpr() {
  auto left = ParseRelationalExpr();
  while (lexer_ptr_->CurrentToken()->type == TokenType::Equal ||
         lexer_ptr_->CurrentToken()->type == TokenType::PipeEqual) {
    BinaryOperator op = BinaryOperator::Equal;
    if (lexer_ptr_->CurrentToken()->type == TokenType::PipeEqual) {
      op = BinaryOperator::PipeEqual;
    }
    lexer_ptr_->GetNextToken();
    auto node = std::make_shared<BinaryNode>(op, left, ParseRelationalExpr());
    left = node;
  }
  return left;
}

std::shared_ptr<AstNode> Parser::ParseRelationalExpr() {
  auto left = ParseAddExpr();
  while (lexer_ptr_->CurrentToken()->type == TokenType::Greater ||
         lexer_ptr_->CurrentToken()->type == TokenType::GreaterEqual ||
         lexer_ptr_->CurrentToken()->type == TokenType::Lesser ||
         lexer_ptr_->CurrentToken()->type == TokenType::LesserEqual) {
    BinaryOperator op = BinaryOperator::Greater;
    if (lexer_ptr_->CurrentToken()->type == TokenType::GreaterEqual) {
      op = BinaryOperator::GreaterEqual;
    } else if (lexer_ptr_->CurrentToken()->type == TokenType::Lesser) {
      op = BinaryOperator::Lesser;
    } else if (lexer_ptr_->CurrentToken()->type == TokenType::LesserEqual) {
      op = BinaryOperator::LesserEqual;
    }
    lexer_ptr_->GetNextToken();
    auto node = std::make_shared<BinaryNode>(op, left, ParseAddExpr());
    left = node;
  }
  return left;
}

std::shared_ptr<AstNode> Parser::ParseAddExpr() {
  std::shared_ptr<AstNode> left = ParseMultiExpr();
  while (lexer_ptr_->CurrentToken()->type == TokenType::Add ||
         lexer_ptr_->CurrentToken()->type == TokenType::Sub) {
    BinaryOperator op = lexer_ptr_->CurrentToken()->type == TokenType::Add
                            ? BinaryOperator::Add
                            : BinaryOperator::Sub;
    lexer_ptr_->GetNextToken();
    auto node = std::make_shared<BinaryNode>(op, left, ParseMultiExpr());
    left = node;
  }
  return left;
}

std::shared_ptr<AstNode> Parser::ParseMultiExpr() {
  std::shared_ptr<AstNode> left = ParsePrimaryExpr();
  while (lexer_ptr_->CurrentToken()->type == TokenType::Mul ||
         lexer_ptr_->CurrentToken()->type == TokenType::Div) {
    BinaryOperator op = lexer_ptr_->CurrentToken()->type == TokenType::Mul
                            ? BinaryOperator::Mul
                            : BinaryOperator::Div;
    lexer_ptr_->GetNextToken();
    auto node = std::make_shared<BinaryNode>(op, left, ParsePrimaryExpr());
    left = node;
  }
  return left;
}

std::shared_ptr<AstNode> Parser::ParsePrimaryExpr() {
  if (lexer_ptr_->CurrentToken()->type == TokenType::LParent) {
    lexer_ptr_->GetNextToken();                   // skip (
    auto node = ParseExpr();                      // parse expr
    lexer_ptr_->ExpectToken(TokenType::RParent);  // expect ) and stkip
    return node;
  } else if (lexer_ptr_->CurrentToken()->type == TokenType::Identifier) {
    auto name = lexer_ptr_->CurrentToken()->content;
    auto id = FindId(name);
    if (!id) {
      id = MakeId(name);
    }
    auto node = std::make_shared<IdentifierNode>(id);
    lexer_ptr_->GetNextToken();
    return node;
  } else if (lexer_ptr_->CurrentToken()->type == TokenType::Num) {
    auto node =
        std::make_shared<ConstantNode>(lexer_ptr_->CurrentToken()->value);
    lexer_ptr_->GetNextToken();
    return node;
  } else {
    auto token = lexer_ptr_->CurrentToken();
    DiagnosticError(lexer_ptr_->SourceCode(), token->location.line,
                    token->location.col, "Not support node");
  }
}

std::shared_ptr<Identifier> Parser::FindId(std::string_view name) {
  if (ids_map_.find(name) != ids_map_.end()) {
    return ids_map_[name];
  }
  return nullptr;
}

std::shared_ptr<Identifier> Parser::MakeId(std::string_view name) {
  auto id = std::make_shared<Identifier>(name, 0);
  ids_->push_front(id);
  ids_map_[name] = id;
  return id;
}

}  // namespace bfcc
}  // namespace highkyck
