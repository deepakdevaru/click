// -*- c-basic-offset: 4 -*-
/*
 * script.{cc,hh} -- element provides scripting functionality
 * Eddie Kohler
 *
 * Copyright (c) 2001 International Computer Science Institute
 * Copyright (c) 2001 Mazu Networks, Inc.
 * Copyright (c) 2005 Regents of the University of California
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "script.hh"
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/straccum.hh>
#include <click/handlercall.hh>
#include <click/nameinfo.hh>
#if CLICK_USERLEVEL
# include <signal.h>
# include <click/master.hh>
#endif
CLICK_DECLS

static const StaticNameDB::Entry instruction_entries[] = {
    { "end", Script::INSN_END },
    { "exit", Script::INSN_EXIT },
    { "goto", Script::INSN_GOTO },
    { "label", Script::INSN_LABEL },
    { "loop", Script::INSN_LOOP_PSEUDO },
    { "pause", Script::INSN_WAIT_STEP },
    { "print", Script::INSN_PRINT },
    { "read", Script::INSN_READ },
    { "return", Script::INSN_RETURN },
    { "set", Script::INSN_SET },
    { "stop", Script::INSN_STOP },
    { "wait", Script::INSN_WAIT_PSEUDO },
    { "wait_for", Script::INSN_WAIT_TIME },
    { "wait_step", Script::INSN_WAIT_STEP },
    { "wait_stop", Script::INSN_WAIT_STEP },
    { "wait_time", Script::INSN_WAIT_TIME },
    { "write", Script::INSN_WRITE }
};

#if CLICK_USERLEVEL
static const StaticNameDB::Entry signal_entries[] = {
    { "ABRT", SIGABRT },
    { "HUP", SIGHUP },
    { "INT", SIGINT },
    { "PIPE", SIGPIPE },
    { "QUIT", SIGQUIT },
    { "TERM", SIGTERM },
    { "TSTP", SIGTSTP },
    { "USR1", SIGUSR1 },
    { "USR2", SIGUSR2 }
};
#endif

static NameDB *dbs[2];

void
Script::static_initialize()
{
    dbs[0] = new StaticNameDB(NameInfo::T_SCRIPT_INSN, String(), instruction_entries, sizeof(instruction_entries) / sizeof(instruction_entries[0]));
    NameInfo::installdb(dbs[0], 0);
#if CLICK_USERLEVEL
    dbs[1] = new StaticNameDB(NameInfo::T_SIGNO, String(), signal_entries, sizeof(signal_entries) / sizeof(signal_entries[0]));
    NameInfo::installdb(dbs[1], 0);
#endif
}

void
Script::static_cleanup()
{
    NameInfo::removedb(dbs[0]);
    delete dbs[0];
#if CLICK_USERLEVEL
    NameInfo::removedb(dbs[1]);
    delete dbs[1];
#endif
}

Script::Script()
    : _type(TYPE_ACTIVE), _timer(this), _cur_steps(0)
{
}

Script::~Script()
{
}

void
Script::add_insn(int insn, int arg, int arg2, const String &arg3)
{
    // first instruction must be WAIT or WAIT_STEP, so add INITIAL if
    // necessary
    if (_insns.size() == 0 && insn > INSN_WAIT_TIME)
	add_insn(INSN_INITIAL, 0);
    _insns.push_back(insn);
    _args.push_back(arg);
    _args2.push_back(arg2);
    _args3.push_back(arg3);
}

int
Script::find_label(const String &label) const
{
    for (int i = 0; i < _insns.size(); i++)
	if (_insns[i] == INSN_LABEL && _args3[i] == label)
	    return i;
    return _insns.size();
}

int
Script::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String type_arg;
    if (cp_va_parse_remove_keywords
	(conf, 0, this, errh,
	 "TYPE", cpArgument, "type of script", &type_arg,
	 cpEnd) < 0)
	return -1;

    int before = errh->nerrors();

    String type_word = cp_pop_spacevec(type_arg);
    if (type_word == "ACTIVE" && !type_arg)
	_type = TYPE_ACTIVE;
    else if (type_word == "PASSIVE" && !type_arg)
	_type = TYPE_PASSIVE;
    else if (type_word == "DRIVER" && !type_arg)
	_type = TYPE_DRIVER;
#if CLICK_USERLEVEL
    else if (type_word == "SIGNAL") {
	_type = TYPE_SIGNAL;
	int32_t signo;
	while ((type_word = cp_pop_spacevec(type_arg))
	       && NameInfo::query_int(NameInfo::T_SIGNO, this, type_word, &signo)
	       && signo >= 0 && signo < 32)
	    _signos.push_back(signo);
	if (type_word || !_signos.size())
	    return errh->error("bad signal number");
    }
#endif
    else if (type_word)
	return errh->error("bad TYPE; expected 'ACTIVE', 'DRIVER', or (userlevel) 'SIGNAL'");
    
    if (_type == TYPE_DRIVER) {
	if (router()->attachment("Script"))
	    return errh->error("router has more than one Script element");
	router()->set_attachment("Script", this);
    }

    for (int i = 0; i < conf.size(); i++) {
	String insn_name = cp_pop_spacevec(conf[i]);
	int32_t insn;
	if (!insn_name)		// ignore as benign
	    continue;
	else if (!NameInfo::query_int(NameInfo::T_SCRIPT_INSN, this, insn_name, &insn)) {
	    errh->error("syntax error at '%s'", insn_name.c_str());
	    continue;
	}

	switch (insn) {

	case INSN_WAIT_PSEUDO:
	    if (!conf[i])
		goto wait_step;
	    else
		goto wait_time;

	wait_step:
	case INSN_WAIT_STEP: {
	    unsigned n = 1;
	    if (!conf[i] || cp_unsigned(conf[i], &n))
		add_insn(INSN_WAIT_STEP, n, 0);
	    else
		goto syntax_error;
	    break;
	}

	case INSN_WRITE:
	case INSN_READ:
	case INSN_PRINT:
	case INSN_GOTO:
	    add_insn(insn, 0, 0, conf[i]);
	    break;

	case INSN_RETURN:
	    conf[i] = "_ " + conf[i];
	    /* fall through */
	case INSN_SET: {
	    String word = cp_pop_spacevec(conf[i]);
	    if (!word || (insn == INSN_SET && !conf[i]))
		goto syntax_error;
	    int x;
	    for (x = 0; x < _vars.size(); x += 2)
		if (_vars[x] == word)
		    goto found_vars;
	    _vars.push_back(word);
	    _vars.push_back(String());
	found_vars:
	    add_insn(insn, x, 0, conf[i]);
	    break;
	}

	wait_time:
	case INSN_WAIT_TIME: {
	    Timestamp ts;
	    if (cp_time(conf[i], &ts))
		add_insn(INSN_WAIT_TIME, ts.sec(), ts.subsec());
	    else
		goto syntax_error;
	    break;
	}

	case INSN_LABEL: {
	    String word = cp_pop_spacevec(conf[i]);
	    if (!word || conf[i])
		goto syntax_error;
	    add_insn(insn, 0, 0, word);
	    break;
	}
	    
	case INSN_LOOP_PSEUDO:
	    insn = INSN_GOTO;
	    /* fallthru */
	case INSN_END:
	case INSN_EXIT:
	case INSN_STOP:
	    if (conf[i])
		goto syntax_error;
	    add_insn(insn, 0);
	    break;

	default:
	syntax_error:
	    errh->error("syntax error at '%s'", insn_name.c_str());
	    break;

	}
    }

    // fix the gotos
    for (int i = 0; i < _insns.size(); i++)
	if (_insns[i] == INSN_GOTO && _args3[i]) {
	    String word = cp_pop_spacevec(_args3[i]);
	    if ((_args[i] = find_label(word)) >= _insns.size())
		errh->error("no such label '%s'", word.c_str());
	}
    
    if (_insns.size() == 0 && _type == TYPE_DRIVER)
	add_insn(INSN_WAIT_STEP, 1, 0);
    add_insn(_type == TYPE_DRIVER ? INSN_STOP : INSN_END, 0);

    return (errh->nerrors() == before ? 0 : -1);
}

int
Script::initialize(ErrorHandler *)
{
    _insn_pos = 0;
    _step_count = 0;
    _timer.initialize(this);

    int insn = _insns[_insn_pos];
    assert(insn <= INSN_WAIT_TIME);
    if (_type == TYPE_SIGNAL || _type == TYPE_PASSIVE)
	/* passive, do nothing */;
    else if (insn == INSN_WAIT_TIME)
	_timer.schedule_after(Timestamp(_args[_insn_pos], _args2[_insn_pos]));
    else if (insn == INSN_INITIAL && _type == TYPE_DRIVER) {
	// get rid of the initial runcount so we get called right away
	router()->adjust_runcount(-1);
	_args[0] = 1;
    } else if (insn == INSN_INITIAL)
	_timer.schedule_now();

#if CLICK_USERLEVEL
    if (_type == TYPE_SIGNAL)
	for (int i = 0; i < _signos.size(); i++)
	    master()->add_signal_handler(_signos[i], router(), id() + ".run");
#endif
    
    return 0;
}

int
Script::step(int nsteps, int step_type, int njumps)
{
    ErrorHandler *errh = ErrorHandler::default_handler();
    ContextErrorHandler cerrh(errh, "While executing '" + declaration() + "':");
    Expander expander;
    expander.script = this;
    expander.errh = &cerrh;

    nsteps += _step_count;
    while ((nsteps - _step_count) >= 0 && _insn_pos < _insns.size()
	   && njumps < MAX_JUMPS) {

	// process current instruction
	// increment instruction pointer now, in case we call 'goto' directly
	// or indirectly
	int ipos = _insn_pos++;
	int insn = _insns[ipos];

	switch (insn) {

	case INSN_STOP:
	    _step_count++;
	    _insn_pos--;
	    if (step_type != STEP_ROUTER)
		router()->adjust_runcount(-1);
	    return njumps + 1;

	case INSN_WAIT_STEP:
	    while (_step_count < nsteps && _args2[ipos] < _args[ipos]) {
		_args2[ipos]++;
		_step_count++;
	    }
	    if (_step_count == nsteps && _args2[ipos] < _args[ipos]) {
		_insn_pos--;
		goto done;
	    }
	    break;

	case INSN_WAIT_TIME:
	    if (_step_count == nsteps) {
		_timer.schedule_after(Timestamp(_args[ipos], _args2[ipos]));
		_insn_pos--;
		goto done;
	    }
	    _step_count++;
	    _timer.unschedule();
	    break;

	case INSN_INITIAL:
	    if (_args[ipos]) {
		_step_count++;
		_args[ipos] = 0;
	    }
	    break;

	case INSN_PRINT: {
	    String text = _args3[ipos];
	    
#if CLICK_USERLEVEL || CLICK_TOOL
	    FILE *f = stdout;
	    if (text.length() && text[0] == '>') {
		bool append = (text.length() > 1 && text[1] == '>');
		text = text.substring(1 + append);
		String filename = cp_pop_spacevec(text);
		if (filename && filename != "-"
		    && !(f = fopen(filename.c_str(), append ? "ab" : "wb"))) {
		    errh->error("%s: %s", filename.c_str(), strerror(errno));
		    break;
		}
	    }
#endif

	    int before = cerrh.nerrors();
	    String result = cp_expand(text, expander);
	    if (text && (isalpha(text[0]) || text[0] == '@' || text[0] == '_'))
		result = HandlerCall::call_read(result, this, &cerrh);
	    else
		result = cp_unquote(result);
	    if (cerrh.nerrors() == before && (!result || result.back() != '\n'))
		result += "\n";
	    
#if CLICK_USERLEVEL || CLICK_TOOL
	    fwrite(result.data(), 1, result.length(), f);
	    if (f != stdout)
		fclose(f);
#else
	    errh->message("%s", result.c_str());
#endif
	    break;
	}
	    
	case INSN_READ: {
	    HandlerCall hc(cp_expand(_args3[ipos], expander));
	    if (hc.initialize_read(this, &cerrh) >= 0) {
		String result = hc.call_read();
		errh->message("%s:\n%s\n", hc.handler()->unparse_name(hc.element()).c_str(), result.c_str());
	    }
	    break;
	}

	case INSN_WRITE: {
	    HandlerCall hc(cp_expand(_args3[ipos], expander));
	    if (hc.initialize_write(this, &cerrh) >= 0)
		(void) hc.call_write(&cerrh);
	    break;
	}

	case INSN_RETURN:
	case INSN_SET: {
	    expander.errh = errh;
	    _vars[_args[ipos] + 1] = cp_expand(_args3[ipos], expander);
	    if (insn == INSN_RETURN && _insn_pos == ipos + 1) {
		_insn_pos--;
		goto done;
	    }
	    break;
	}
	    
	case INSN_GOTO: {
	    // reset intervening instructions
	    String cond_text = cp_expand(_args3[ipos], expander);
	    bool cond;
	    if (cond_text && !cp_bool(cond_text, &cond))
		cerrh.error("bad condition");
	    else if (!cond_text || cond) {
		for (int i = _args[ipos]; i < ipos; i++)
		    if (_insns[i] == INSN_WAIT_STEP)
			_args2[i] = 0;
		_insn_pos = _args[ipos];
	    }
	    break;
	}

	case INSN_END:
#if CLICK_USERLEVEL
	    if (_type == TYPE_SIGNAL)
		for (int i = 0; i < _signos.size(); i++)
		    master()->add_signal_handler(_signos[i], router(), id() + ".run");
#endif
	    /* fallthru */
	case INSN_EXIT:
	    _insn_pos--;
	    goto done;
	    
	}

	if (_insn_pos != ipos + 1)
	    njumps++;
    }

  done:
    if (njumps >= MAX_JUMPS) {
	ErrorHandler::default_handler()->error("%{element}: too many jumps, giving up", this);
	_insn_pos = _insns.size();
	_timer.unschedule();
    }
    if (step_type == STEP_ROUTER)
	router()->adjust_runcount(1);
    return njumps + 1;
}

void
Script::run_timer(Timer *)
{
    // called when a timer expires
    assert(_insns[_insn_pos] == INSN_WAIT_TIME || _insns[_insn_pos] == INSN_INITIAL);
    step(1, STEP_TIMER, 0);
}

bool
Script::Expander::expand(const String &vname, int vartype, int quote, StringAccum &sa)
{
    for (int i = 0; i < script->_vars.size(); i += 2)
	if (script->_vars[i] == vname) {
	    sa << cp_expand_in_quotes(script->_vars[i + 1], quote);
	    return true;
	}
    
    if (vartype == '(') {
	HandlerCall hc(vname);
	if (hc.initialize_read(script, errh) >= 0) {
	    sa << cp_expand_in_quotes(hc.call_read(), quote);
	    return true;
	}
    }
    
    return false;
}

enum {
    ST_STEP = 0, ST_RUN, ST_GOTO,
    AR_ADD = 0, AR_SUB,
    AR_LT, AR_EQ, AR_GT, AR_GE, AR_NE, AR_LE, // order is important
    AR_FIRST
};

int
Script::step_handler(int, String &str, Element *e, const Handler *h, ErrorHandler *errh)
{
    Script *scr = (Script *) e;
    String data = cp_uncomment(str);
    int nsteps, steptype;
    int what = (uintptr_t) h->thunk1();

    if (what == ST_GOTO) {
	int step = scr->find_label(cp_uncomment(data));
	if (step >= scr->_insns.size())
	    return errh->error("jump to nonexistent label");
	for (int i = step; i < scr->_insns.size(); i++)
	    if (scr->_insns[i] == INSN_WAIT_STEP)
		scr->_args2[i] = 0;
	scr->_insn_pos = step;
	nsteps = 0, steptype = STEP_JUMP;
    } else if (what == ST_RUN) {
	scr->_insn_pos = 0;
	nsteps = 0, steptype = STEP_JUMP;
    } else {
	if (data == "router")
	    nsteps = 1, steptype = STEP_ROUTER;
	else if (!data)
	    nsteps = 1, steptype = STEP_NORMAL;
	else if (cp_integer(data, &nsteps))
	    steptype = STEP_NORMAL;
	else
	    return errh->error("syntax error");
    }

    if (!scr->_cur_steps) {
	int cur_steps = nsteps, njumps = 0;
	scr->_cur_steps = &cur_steps;
	while ((nsteps = cur_steps) >= 0) {
	    cur_steps = -1;
	    njumps = scr->step(nsteps, steptype, njumps);
	    steptype = ST_STEP;
	}
	scr->_cur_steps = 0;
    } else if (what == ST_STEP)
	*scr->_cur_steps += nsteps;

    int last_insn = (scr->_insn_pos < scr->_insns.size() ? scr->_insns[scr->_insn_pos] : -1);
    
    str = String();
    if (last_insn == INSN_RETURN)
	for (int i = 0; i < scr->_vars.size(); i += 2)
	    if (scr->_vars[i] == "_")
		str = scr->_vars[i + 1];
    
    return (last_insn == INSN_STOP);
}

int
Script::arithmetic_handler(int, String &str, Element *, const Handler *h, ErrorHandler *errh)
{
#if HAVE_INT64_TYPES
    int64_t accum = 0, arg;
#else
    int32_t accum = 0, arg;
#endif
    bool first = true;
    int what = (uintptr_t) h->thunk1();

    switch (what) {

    case AR_ADD:
    case AR_SUB:
	while (1) {
	    String word = cp_pop_spacevec(str);
	    if (!word && cp_is_space(str))
		break;
	    if (!cp_integer(word, &arg))
		return errh->error("expected list of numbers");
	    accum += (what == AR_ADD || first ? arg : -arg);
	    first = false;
	}
	str = String(accum);
	return 0;

    case AR_FIRST:
	str = cp_pop_spacevec(str);
	return 0;

    case AR_LT:
    case AR_EQ:
    case AR_GT:
    case AR_LE:
    case AR_NE:
    case AR_GE: {
	String a = cp_pop_spacevec(str);
	String b = cp_pop_spacevec(str);
	if (str || !cp_integer(a, &accum) || !cp_integer(b, &arg))
	    return errh->error("syntax error");
	int x = (accum < arg ? AR_LT : (accum == arg ? AR_EQ : AR_GT));
	if (what == x || (what >= AR_GE && what != x + 3))
	    str = String::stable_string("true", 4);
	else
	    str = String::stable_string("false", 5);
	return 0;
    }
	
    }

    return -1;
}

void
Script::add_handlers()
{
    set_handler("step", Handler::OP_WRITE | Handler::ONE_HOOK, step_handler, (void *) ST_STEP, 0);
    set_handler("goto", Handler::OP_WRITE | Handler::ONE_HOOK, step_handler, (void *) ST_GOTO, 0);
    set_handler("run", Handler::OP_READ | Handler::READ_PARAM | Handler::OP_WRITE | Handler::ONE_HOOK, step_handler, (void *) ST_RUN, 0);
    set_handler("add", Handler::OP_READ | Handler::READ_PARAM | Handler::ONE_HOOK, arithmetic_handler, (void *) AR_ADD, 0);
    set_handler("sub", Handler::OP_READ | Handler::READ_PARAM | Handler::ONE_HOOK, arithmetic_handler, (void *) AR_SUB, 0);
    set_handler("eq", Handler::OP_READ | Handler::READ_PARAM | Handler::ONE_HOOK, arithmetic_handler, (void *) AR_EQ, 0);
    set_handler("ne", Handler::OP_READ | Handler::READ_PARAM | Handler::ONE_HOOK, arithmetic_handler, (void *) AR_NE, 0);
    set_handler("gt", Handler::OP_READ | Handler::READ_PARAM | Handler::ONE_HOOK, arithmetic_handler, (void *) AR_GT, 0);
    set_handler("ge", Handler::OP_READ | Handler::READ_PARAM | Handler::ONE_HOOK, arithmetic_handler, (void *) AR_GE, 0);
    set_handler("lt", Handler::OP_READ | Handler::READ_PARAM | Handler::ONE_HOOK, arithmetic_handler, (void *) AR_LT, 0);
    set_handler("le", Handler::OP_READ | Handler::READ_PARAM | Handler::ONE_HOOK, arithmetic_handler, (void *) AR_LE, 0);
    set_handler("first", Handler::OP_READ | Handler::READ_PARAM | Handler::ONE_HOOK, arithmetic_handler, (void *) AR_FIRST, 0);
}

EXPORT_ELEMENT(Script)
CLICK_ENDDECLS
