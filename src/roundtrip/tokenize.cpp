#include "codegen.h"
#include "roundtrip.h"

namespace imforge {
namespace roundtrip {

// reuse codegen's tokenizer so token indices line up with LiteralRef::block_token_index
std::vector<Token> Tokenize(const std::string& cpp) {
    std::vector<Token> out;
    for (const codegen::Token& t : codegen::Tokenize(cpp)) {
        Token::Kind kind = Token::Punct;
        switch (t.kind) {
            case codegen::Token::Kind::Ident: kind = Token::Ident; break;
            case codegen::Token::Kind::Number: kind = Token::Number; break;
            case codegen::Token::Kind::String:
                kind = !t.text.empty() && t.text[0] == '\'' ? Token::Char : Token::String;
                break;
            default: kind = Token::Punct; break;
        }
        out.push_back(Token{kind, t.text, t.line, t.offset});
    }
    return out;
}

}  // namespace roundtrip
}  // namespace imforge
