// -*- c-basic-offset: 4 -*-
#ifndef CLICK_LEXERT_HH
#define CLICK_LEXERT_HH
#include <click/error.hh>
#include <click/hashmap.hh>
#include <stdio.h>
class RouterT;
class ElementClassT;
class StringAccum;
class LexerTInfo;

enum {
    lexEOF = 0,
    lexIdent = 256,
    lexVariable,
    lexConfig,
    lexArrow,
    lex2Colon,
    lex2Bar,
    lex3Dot,
    lexTunnel,
    lexElementclass,
    lexRequire
};

class Lexeme { public:

    Lexeme()				: _kind(lexEOF) { }
    Lexeme(int k, const String &s, const char *p) : _kind(k), _s(s), _pos(p) { }
    
    int kind() const			{ return _kind; }
    bool is(int k) const		{ return _kind == k; }
    operator bool() const		{ return _kind != lexEOF; }
    
    const String &string() const	{ return _s; }
    String &string()			{ return _s; }

    const char *pos1() const		{ return _pos; }
    const char *pos2() const		{ return _pos + _s.length(); }
  
  private:
  
    int _kind;
    String _s;
    const char *_pos;

};

class LexerT { public:

    LexerT(ErrorHandler * = 0, bool ignore_line_directives = false);
    virtual ~LexerT();
  
    void reset(const String &data, const String &filename = String());
    void clear();
    void set_lexinfo(LexerTInfo *);
    void ignore_line_directives(bool g)	{ _ignore_line_directives = g; }

    String remaining_text() const;
    void set_remaining_text(const String &);
  
    const Lexeme &lex();
    void unlex(const Lexeme &);
    Lexeme lex_config();
    String landmark() const;
  
    bool yport(int &port, const char *&pos1, const char *&pos2);
    bool yelement(int &element, bool comma_ok);
    void ydeclaration(const Lexeme &first_element = Lexeme());
    bool yconnection();
    void ycompound_arguments(RouterT *);
    void yelementclass(const char *pos1);
    void ytunnel();
    ElementClassT *ycompound(String, const char *decl_pos1, const char *name_pos1);
    void yrequire();
    bool ystatement(bool nested = false);

    RouterT *router() const		{ return _router; }
    RouterT *finish();
  
  protected:
  
    // lexer
    String _big_string;
  
    const char *_data;
    const char *_end;
    const char *_pos;
  
    String _filename;
    String _original_filename;
    unsigned _lineno;
    bool _ignore_line_directives;
    
    bool get_data();
    const char *skip_line(const char *);
    const char *skip_slash_star(const char *);
    const char *skip_backslash_angle(const char *);
    const char *skip_quote(const char *, char);
    const char *process_line_directive(const char *);
    Lexeme next_lexeme();
    static String lexeme_string(int);
  
    // parser
    enum { TCIRCLE_SIZE = 8 };
    Lexeme _tcircle[TCIRCLE_SIZE];
    int _tpos;
    int _tfull;
  
    // router
    RouterT *_router;
    
    int _anonymous_offset;
  
    // what names represent types? (builds up linearly)
    HashMap<String, ElementClassT *> _base_type_map;

    // errors
    LexerTInfo *_lexinfo;
    ErrorHandler *_errh;

    void vlerror(const char *, const char *, const String &, const char *, va_list);
    int lerror(const char *, const char *, const char *, ...);
    int lerror(const Lexeme &, const char *, ...);
    String anon_element_name(const String &) const;

    bool expect(int, bool report_error = true);
    const char *next_pos() const;
    
    ElementClassT *element_type(const Lexeme &) const;
    ElementClassT *force_element_type(const Lexeme &);

    LexerT(const LexerT &);
    LexerT &operator=(const LexerT &);
    int make_element(String, const Lexeme &, const char *decl_pos2, ElementClassT *, const String &, const String &);
    int make_anon_element(const Lexeme &, const char *decl_pos2, ElementClassT *, const String &, const String &);
    void connect(int f1, int p1, int p2, int f2);
  
};

#endif
