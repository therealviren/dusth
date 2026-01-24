#include "parser.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <signal.h>

typedef struct { const char* start; const char* current; } Parser;
static Parser parser;

static void error(const char* message);
static void* safe_malloc(size_t size);
static char* safe_strdup(const char* s, size_t len);
static Node* new_node(NodeType type);
static void add_child(Node* parent, Node* child);
static char peek();
static char advance();
static bool is_at_end();
static void skip_whitespace();
static bool check(const char* keyword);
static bool match_keyword(const char* keyword);
static bool match_char(char expected);
static void expect_char(char expected, const char* err_msg);
static Node* parse_expr();
static Node* parse_stmt();
static Node* parse_block();
static Node* parse_primary();
static Node* parse_primary_with_postfix();
static Node* parse_postfix(Node* left);
static Node* parse_unary();
static Node* parse_factor();
static Node* parse_term();
static Node* parse_comparison();
static Node* parse_function();
static Node* parse_extern();
static Node* parse_import();

static void handle_interrupt(int sig) { (void)sig; exit(0); }

static void error(const char* message) {
    fprintf(stderr, "Parse Error: %s\n", message);
    fprintf(stderr, "Position: %.40s\n", parser.current);
    exit(1);
}

static void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) error("Memory allocation failed");
    return ptr;
}

static char* safe_strdup(const char* s, size_t len) {
    char* str = safe_malloc(len + 1);
    memcpy(str, s, len);
    str[len] = '\0';
    return str;
}

static Node* new_node(NodeType type) {
    Node* node = safe_malloc(sizeof(Node));
    node->type = type;
    node->children = NULL;
    node->childc = 0;
    node->capacity = 0;
    node->text = NULL;
    node->num = 0.0;
    return node;
}

static void add_child(Node* parent, Node* child) {
    if (!child) return;
    if (parent->childc + 1 > parent->capacity) {
        size_t new_cap = parent->capacity < 8 ? 8 : parent->capacity * 2;
        Node** new_children = safe_malloc(sizeof(Node*) * new_cap);
        if (parent->children) {
            memcpy(new_children, parent->children, sizeof(Node*) * parent->childc);
            free(parent->children);
        }
        parent->children = new_children;
        parent->capacity = new_cap;
    }
    parent->children[parent->childc++] = child;
}

void free_node(Node* n) {
    if (!n) return;
    for (size_t i = 0; i < n->childc; i++) free_node(n->children[i]);
    if (n->children) free(n->children);
    if (n->text) free(n->text);
    free(n);
}

static char peek() { return *parser.current; }

static char advance() {
    if (is_at_end()) return '\0';
    return *parser.current++;
}

static bool is_at_end() { return *parser.current == '\0'; }

static void skip_whitespace() {
    for (;;) {
        char c = peek();
        if (isspace((unsigned char)c)) advance();
        else if (c == '/' && parser.current[1] == '/') {
            while (peek() != '\n' && !is_at_end()) advance();
        } else break;
    }
}

static bool check(const char* keyword) {
    size_t len = strlen(keyword);
    if (strncmp(parser.current, keyword, len) == 0) {
        char after = parser.current[len];
        return !isalnum((unsigned char)after) && after != '_';
    }
    return false;
}

static bool match_keyword(const char* keyword) {
    skip_whitespace();
    if (check(keyword)) {
        parser.current += strlen(keyword);
        return true;
    }
    return false;
}

static bool match_char(char expected) {
    skip_whitespace();
    if (peek() == expected) {
        advance();
        return true;
    }
    return false;
}

static void expect_char(char expected, const char* err_msg) {
    if (!match_char(expected)) error(err_msg);
}

Node* node_clone(Node* n){
    if(!n) return NULL;
    Node* c = malloc(sizeof(Node));
    c->type = n->type;
    c->childc = n->childc;
    c->capacity = n->childc;
    c->text = n->text ? safe_strdup(n->text, strlen(n->text)) : NULL;
    c->num = n->num;
    c->children = NULL;
    if(n->childc){
        c->children = malloc(sizeof(Node*) * n->childc);
        for(size_t i = 0; i < n->childc; i++) c->children[i] = node_clone(n->children[i]);
    }
    return c;
}

static Node* parse_primary() {
    skip_whitespace();
    char c = peek();

    if (c == '"') {
        advance();
        size_t buf_size = 16;
        size_t len = 0;
        char* buf = safe_malloc(buf_size);
        while (!is_at_end() && peek() != '"') {
            char ch = advance();
            if (ch == '\\') {
                char esc = advance();
                if (esc == 'n') ch = '\n';
                else if (esc == 't') ch = '\t';
                else if (esc == 'r') ch = '\r';
                else if (esc == '\\') ch = '\\';
                else if (esc == '"') ch = '"';
                else if (esc == 'x') {
                    char h1 = advance();
                    char h2 = advance();
                    char hex[3] = { h1, h2, 0 };
                    ch = (char)strtol(hex, NULL, 16);
                } else {
                    ch = esc;
                }
            }
            if (len + 1 >= buf_size) {
                buf_size *= 2;
                buf = realloc(buf, buf_size);
                if (!buf) error("Memory allocation failed");
            }
            buf[len++] = ch;
        }
        expect_char('"', "Expected closing '\"'");
        buf[len] = '\0';
        Node* node = new_node(NODE_LITERAL);
        node->text = buf;
        return node;
    }

    if (c == '(') {
        advance();
        Node* inner = parse_expr();
        expect_char(')', "Expected ')'");
        return inner;
    }

    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)parser.current[1]))) {
        const char* start = parser.current;
        while (isdigit((unsigned char)peek()) || peek() == '.') advance();
        Node* node = new_node(NODE_LITERAL);
        node->num = strtod(start, NULL);
        return node;
    }

    if (isalpha((unsigned char)c) || c == '_') {
        const char* start = parser.current;
        while (isalnum((unsigned char)peek()) || peek() == '_') advance();
        Node* node = new_node(NODE_IDENT);
        node->text = safe_strdup(start, parser.current - start);
        return node;
    }

    return NULL;
}

static Node* make_call_node_from_expr(Node* callee) {
    Node* n = new_node(NODE_CALL);
    n->text = NULL;
    add_child(n, callee);
    return n;
}

static Node* make_call_node(Node* callee) {
    Node* n = new_node(NODE_CALL);
    add_child(n, callee);
    return n;
}

static Node* parse_postfix(Node* left) {
    while (true) {
        skip_whitespace();
        char c = peek();

        if (c == '[') {
            advance();
            Node* index = parse_expr();
            expect_char(']', "Expected ']'");
            Node* n = new_node(NODE_INDEX);
            add_child(n, left);
            add_child(n, index);
            left = n;
            continue;
        }

        if (c == '(') {
            advance();
            Node* call = make_call_node(left);
            skip_whitespace();
            while (peek() != ')' && !is_at_end()) {
                Node* arg = parse_expr();
                if (!arg) break;
                add_child(call, arg);
                skip_whitespace();
                match_char(',');
            }
            expect_char(')', "Expected ')'");
            left = call;
            continue;
        }

        if (c == '.' && parser.current[1] != '\0') {
            const char* name_start = parser.current + 1;
            if (isalpha((unsigned char)parser.current[1]) || parser.current[1] == '_') {
                parser.current++;
                while (isalnum((unsigned char)peek()) || peek() == '_') advance();
                Node* member = new_node(NODE_IDENT);
                member->text = safe_strdup(name_start, parser.current - name_start);
                Node* n = new_node(NODE_MEMBER);
                add_child(n, left);
                add_child(n, member);
                left = n;

                skip_whitespace();
                if (peek() == '(') {
                    advance();
                    Node* call = make_call_node(left);
                    skip_whitespace();
                    while (peek() != ')' && !is_at_end()) {
                        Node* arg = parse_expr();
                        if (!arg) break;
                        add_child(call, arg);
                        skip_whitespace();
                        match_char(',');
                    }
                    expect_char(')', "Expected ')'");
                    left = call;
                }
                continue;
            }
        }

        break;
    }
    return left;
}

static Node* parse_primary_with_postfix() {
    Node* primary = parse_primary();
    if (!primary) return NULL;
    return parse_postfix(primary);
}

static Node* parse_unary() {
    skip_whitespace();
    if (match_char('-')) {
        Node* node = new_node(NODE_UNARY);
        node->text = safe_strdup("-", 1);
        add_child(node, parse_unary());
        return node;
    }
    if (match_char('!')) {
        Node* node = new_node(NODE_UNARY);
        node->text = safe_strdup("!", 1);
        add_child(node, parse_unary());
        return node;
    }
    return parse_primary_with_postfix();
}

static Node* parse_factor() {
    Node* left = parse_unary();
    if (!left) return NULL;

    for (;;) {
        skip_whitespace();
        char c = peek();
        Node* n = NULL;

        if (c == '*' && parser.current[1] != '=') {
            advance();
            n = new_node(NODE_BINARY);
            n->text = safe_strdup("*", 1);
        } else if (c == '/' && parser.current[1] != '=') {
            advance();
            n = new_node(NODE_BINARY);
            n->text = safe_strdup("/", 1);
        } else {
            break;
        }

        add_child(n, left);
        add_child(n, parse_unary());
        left = n;
    }

    return left;
}

static Node* parse_term() {
    Node* left = parse_factor();
    if (!left) return NULL;

    for (;;) {
        skip_whitespace();
        char c = peek();
        Node* n = NULL;

        if (c == '+' && parser.current[1] != '=') {
            advance();
            n = new_node(NODE_BINARY);
            n->text = safe_strdup("+", 1);
        } else if (c == '-' && parser.current[1] != '=') {
            advance();
            n = new_node(NODE_BINARY);
            n->text = safe_strdup("-", 1);
        } else {
            break;
        }

        add_child(n, left);
        add_child(n, parse_factor());
        left = n;
    }

    return left;
}

static Node* parse_comparison() {
    Node* left = parse_term();
    if (!left) return NULL;

    for (;;) {
        skip_whitespace();
        char c = peek();
        Node* n = NULL;

        if (c == '+' && parser.current[1] == '=') {
            advance(); advance();
            n = new_node(NODE_ASSIGN);
            n->text = safe_strdup("+=", 2);
        } else if (c == '-' && parser.current[1] == '=') {
            advance(); advance();
            n = new_node(NODE_ASSIGN);
            n->text = safe_strdup("-=", 2);
        } else if (c == '*' && parser.current[1] == '=') {
            advance(); advance();
            n = new_node(NODE_ASSIGN);
            n->text = safe_strdup("*=", 2);
        } else if (c == '/' && parser.current[1] == '=') {
            advance(); advance();
            n = new_node(NODE_ASSIGN);
            n->text = safe_strdup("/=", 2);
        } else if (c == '%' && parser.current[1] == '=') {
            advance(); advance();
            n = new_node(NODE_ASSIGN);
            n->text = safe_strdup("%=", 2);
        } else if (c == '=' && parser.current[1] == '=') {
            advance(); advance();
            n = new_node(NODE_BINARY);
            n->text = safe_strdup("==", 2);
        } else if (c == '=' ) {
            advance();
            n = new_node(NODE_ASSIGN);
            n->text = safe_strdup("=", 1);
        } else if (c == '!' && parser.current[1] == '=') {
            advance(); advance();
            n = new_node(NODE_BINARY);
            n->text = safe_strdup("!=", 2);
        } else if (c == '<') {
            advance();
            if (peek() == '=') {
                advance();
                n = new_node(NODE_BINARY);
                n->text = safe_strdup("<=", 2);
            } else {
                n = new_node(NODE_BINARY);
                n->text = safe_strdup("<", 1);
            }
        } else if (c == '>') {
            advance();
            if (peek() == '=') {
                advance();
                n = new_node(NODE_BINARY);
                n->text = safe_strdup(">=", 2);
            } else {
                n = new_node(NODE_BINARY);
                n->text = safe_strdup(">", 1);
            }
        } else {
            break;
        }

        if (!n) break;
        add_child(n, left);
        add_child(n, parse_term());
        left = n;
    }

    return left;
}

static Node* parse_expr() {
    return parse_comparison();
}

static Node* parse_block() {
    expect_char('{', "Block must start with '{'");
    Node* block = new_node(NODE_BLOCK);

    skip_whitespace();
    while (!is_at_end() && peek() != '}') {
        Node* stmt = parse_stmt();
        if (stmt) add_child(block, stmt);
        skip_whitespace();
    }

    expect_char('}', "Block must end with '}'");
    return block;
}

static Node* parse_import() {
    skip_whitespace();
    if (peek() != '"') error("import expects a file string");
    advance();

    const char* start = parser.current;
    while (peek() != '"' && !is_at_end()) advance();
    if (is_at_end()) error("Unterminated import string");

    Node* node = new_node(NODE_IMPORT);
    node->text = safe_strdup(start, parser.current - start);

    expect_char('"', "Unterminated import string");
    match_char(';');

    return node;
}

static Node* parse_function() {
    skip_whitespace();
    Node* node = new_node(NODE_FUNC);

    const char* start = parser.current;
    while (isalnum((unsigned char)peek()) || peek() == '_') advance();
    if (start == parser.current) error("Function must have a name");
    node->text = safe_strdup(start, parser.current - start);

    skip_whitespace();
    expect_char('(', "Function parameters must start with '('");

    if (peek() != ')') {
        do {
            skip_whitespace();
            const char* arg_start = parser.current;
            while (isalnum((unsigned char)peek()) || peek() == '_') advance();
            if (arg_start == parser.current) error("Function parameter name expected");
            Node* arg = new_node(NODE_IDENT);
            arg->text = safe_strdup(arg_start, parser.current - arg_start);
            add_child(node, arg);
        } while(match_char(','));
    }

    expect_char(')', "Function parameters must end with ')'");

    skip_whitespace();
    Node* body = parse_block();
    add_child(node, body);

    return node;
}

static Node* parse_extern() {
    skip_whitespace();
    Node* node = new_node(NODE_EXTERN);

    const char* start = parser.current;
    while (isalnum((unsigned char)peek()) || peek() == '_') advance();
    if (start == parser.current) error("Extern must have a name");
    node->text = safe_strdup(start, parser.current - start);

    skip_whitespace();
    expect_char('(', "Extern parameters must start with '('");

    if (peek() != ')') {
        do {
            skip_whitespace();
            const char* arg_start = parser.current;
            while (isalnum((unsigned char)peek()) || peek() == '_') advance();
            if (arg_start == parser.current) error("Extern parameter name expected");
            Node* arg = new_node(NODE_IDENT);
            arg->text = safe_strdup(arg_start, parser.current - arg_start);
            add_child(node, arg);
        } while(match_char(','));
    }

    expect_char(')', "Extern parameters must end with ')'");
    match_char(';');

    return node;
}

static Node* parse_stmt() {
    skip_whitespace();
    if (is_at_end()) return NULL;

    Node* node = NULL;

    if (match_keyword("let")) {
        node = new_node(NODE_LET);
        skip_whitespace();
        const char* start = parser.current;
        while (isalnum((unsigned char)peek()) || peek() == '_') advance();
        if (start == parser.current) error("Expected variable name after let");
        node->text = safe_strdup(start, parser.current - start);
        skip_whitespace();
        expect_char('=', "Expected '=' after variable name");
        add_child(node, parse_expr());
    } else if (match_keyword("if")) {
        skip_whitespace();
        Node* condition = NULL;
        if (peek() == '(') {
            advance();
            condition = parse_expr();
            expect_char(')', "if expects ')'");
        } else {
            condition = parse_expr();
            if (!condition) error("if expects a condition");
        }
        Node* then_block = parse_block();
        Node* n = new_node(NODE_IF);
        add_child(n, condition);
        add_child(n, then_block);

        skip_whitespace();
        if (match_keyword("else")) {
            skip_whitespace();
            Node* else_node = NULL;
            if (peek() == '{') {
                else_node = parse_block();
            } else {
                else_node = parse_stmt();
            }
            if (else_node) add_child(n, else_node);
        }

        node = n;
    } else if (match_keyword("while")) {
        skip_whitespace();
        if (peek() != '(') error("while expects '('");
        advance();
        Node* condition = parse_expr();
        expect_char(')', "while expects ')'");
        Node* body = parse_block();
        Node* n = new_node(NODE_LOOP);
        add_child(n, condition);
        add_child(n, body);
        node = n;
    } else if (match_keyword("return")) {
        Node* n = new_node(NODE_RETURN);
        skip_whitespace();
        if (peek() != ';') add_child(n, parse_expr());
        node = n;
    } else {
        Node* expr = parse_expr();
        if (expr) {
            node = new_node(NODE_EXPR_STMT);
            add_child(node, expr);
        }
    }

    skip_whitespace();
    match_char(';');

    return node;
}

Node* parse_program(const char* src) {
    signal(SIGINT, handle_interrupt);

    parser.start = src;
    parser.current = src;

    Node* program = new_node(NODE_PROGRAM);

    skip_whitespace();
    while (!is_at_end()) {
        Node* n = NULL;

        if (match_keyword("import")) {
            n = parse_import();
        } else if (match_keyword("fn")) {
            n = parse_function();
        } else if (match_keyword("extern")) {
            n = parse_extern();
        } else {
            n = parse_stmt();
        }

        if (n) add_child(program, n);
        skip_whitespace();
    }

    return program;
}