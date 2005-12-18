// -*- c-basic-offset: 4 -*-
/*
 * eclasst.{cc,hh} -- tool definition of element classes
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Mazu Networks, Inc.
 * Copyright (c) 2001-2003 International Computer Science Institute
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

#include "eclasst.hh"
#include "routert.hh"
#include "elementmap.hh"
#include "processingt.hh"
#include <click/straccum.hh>
#include <click/confparse.hh>
#include <click/variableenv.hh>
#include <stdlib.h>

static String::Initializer string_initializer;
static HashMap<String, int> base_type_map(-1);
static Vector<ElementClassT *> base_types;

typedef ElementTraits Traits;

namespace {

class TraitsElementClassT : public ElementClassT { public:
    TraitsElementClassT(const String &, int component, ...);
    bool primitive() const		{ return false; }
    const ElementTraits *find_traits() const;
  private:
    ElementTraits _the_traits;
};

TraitsElementClassT::TraitsElementClassT(const String &name, int component, ...)
    : ElementClassT(name)
{
    *(_the_traits.component(Traits::D_CLASS)) = name;
    va_list val;
    va_start(val, component);
    while (component > Traits::D_NONE) {
	const char *x = va_arg(val, const char *);
	if (String *val = _the_traits.component(component))
	    *val = x;
	component = va_arg(val, int);
    }
    va_end(val);
    use();			// make sure we hold onto this forever
}

const ElementTraits *
TraitsElementClassT::find_traits() const
{
    return &_the_traits;
}

}

ElementClassT *ElementClassT::the_tunnel_type = new TraitsElementClassT("<tunnel>", Traits::D_PROCESSING, "a/a", Traits::D_FLOW_CODE, "x/y", 0);


ElementClassT::ElementClassT(const String &name)
    : _name(name), _use_count(0), _traits_version(-1)
{
    //fprintf(stderr, "%p: %s\n", this, printable_name_c_str());
}

ElementClassT::~ElementClassT()
{
    //fprintf(stderr, "%p: ~%s\n", this, printable_name_c_str());
}

const char *
ElementClassT::printable_name_c_str()
{
    if (_name)
	return _name.c_str();
    else
	return "<anonymous>";
}

void
ElementClassT::set_base_type(ElementClassT *t)
{
    t->use();
    int i = base_type_map[t->name()];
    if (i < 0) {
	base_type_map.insert(t->name(), base_types.size());
	base_types.push_back(t);
    } else {
	base_types[i]->unuse();
	base_types[i] = t;
    }
}

ElementClassT *
ElementClassT::base_type(const String &name)
{
    if (!name)
	return 0;
    int i = base_type_map[name];
    if (i >= 0)
	return base_types[i];
    ElementClassT *ec = new ElementClassT(name);
    set_base_type(ec);
    return ec;
}


const ElementTraits *
ElementClassT::find_traits() const
{
    ElementMap *em = ElementMap::default_map();
    assert(em);
    return &em->traits(_name);
}

const String &
ElementClassT::package() const
{
    return ElementMap::default_map()->package(traits());
}

String
ElementClassT::documentation_url() const
{
    return ElementMap::default_map()->documentation_url(traits());
}


ElementClassT *
ElementClassT::resolve(int, int, Vector<String> &, ErrorHandler *, const String &)
{
    return this;
}

ElementT *
ElementClassT::direct_expand_element(
	ElementT *e, RouterT *tor, const String &prefix,
	VariableEnvironment &env, ErrorHandler *errh)
{
    assert(!prefix || prefix.back() == '/');
    RouterT *fromr = e->router();
    String new_name = prefix + e->name();
    String new_configuration = cp_expand(e->configuration(), env);

    // check for tunnel
    if (e->tunnel()) {
	assert(this == ElementClassT::tunnel_type());
	// common case -- expanding router into itself
	if (fromr == tor && !prefix)
	    return e;
	// make the tunnel or tunnel pair
	if (e->tunnel_output()) {
	    tor->add_tunnel(new_name,
			    prefix + e->tunnel_output()->name(),
			    e->landmark(), errh);
	    return tor->element(new_name);
	} else
	    return tor->get_element
		(new_name, e->type(), new_configuration, e->landmark());
    }
    
    // otherwise, not tunnel
	  
    // check for common case -- expanding router into itself
    if (fromr == tor && !prefix) {
	e->configuration() = new_configuration;
	e->set_type(this);
	return e;
    }

    // check for old element
    if (ElementT *new_e = tor->element(new_name))
	ElementT::redeclaration_error(errh, "element", new_name, e->landmark(), new_e->landmark());
    
    // add element
    return tor->get_element(new_name, this, new_configuration, e->landmark());
}

ElementT *
ElementClassT::expand_element(
	ElementT *e, RouterT *tor, const String &prefix,
	VariableEnvironment &env, ErrorHandler *errh)
{
    ElementClassT *c = e->type();
    if (c->primitive())
	return c->direct_expand_element(e, tor, prefix, env, errh);

    // if not direct expansion, do some more work
    int inputs_used = e->ninputs();
    int outputs_used = e->noutputs();
    RouterT *fromr = e->router();

    Vector<String> args;
    String new_configuration = cp_expand(e->configuration(), env);
    cp_argvec(new_configuration, args);

    ElementClassT *found_c = c->resolve(inputs_used, outputs_used, args, errh, e->landmark());
    if (!found_c) {		// destroy element
	if (fromr == tor)
	    e->kill();
	return 0;
    }

    return found_c->complex_expand_element
	(e, new_configuration, args,
	 tor, prefix, env, errh);
}

ElementT *
ElementClassT::complex_expand_element(
	ElementT *e, const String &, Vector<String> &,
	RouterT *tor, const String &prefix,
	VariableEnvironment &env, ErrorHandler *errh)
{
    return direct_expand_element(e, tor, prefix, env, errh);
}

String
ElementClassT::unparse_signature(const String &name, const Vector<String> *formal_types, int nargs, int ninputs, int noutputs)
{
    StringAccum sa;
    sa << (name ? name : String("<anonymous>"));
  
    if (formal_types && formal_types->size()) {
	sa << '(';
	for (int i = 0; i < formal_types->size(); i++) {
	    if (i)
		sa << ", ";
	    if ((*formal_types)[i] == "")
		sa << "<arg>";
	    else if ((*formal_types)[i] == "__REST__")
		sa << "...";
	    else
		sa << (*formal_types)[i];
	}
	sa << ')';
    }

    const char *pl_args = (nargs == 1 ? " argument, " : " arguments, ");
    const char *pl_ins = (ninputs == 1 ? " input, " : " inputs, ");
    const char *pl_outs = (noutputs == 1 ? " output" : " outputs");
    sa << '[';
    if (!formal_types && nargs > 0)
	sa << nargs << pl_args;
    sa << ninputs << pl_ins << noutputs << pl_outs;
    sa << ']';
  
    return sa.take_string();
}


SynonymElementClassT::SynonymElementClassT(const String &name, ElementClassT *eclass, RouterT *declaration_scope)
    : ElementClassT(name), _eclass(eclass), _declaration_scope(declaration_scope)
{
    assert(eclass);
}

ElementClassT *
SynonymElementClassT::resolve(int ninputs, int noutputs, Vector<String> &args, ErrorHandler *errh, const String &landmark)
{
    return _eclass->resolve(ninputs, noutputs, args, errh, landmark);
}

ElementT *
SynonymElementClassT::complex_expand_element(
	ElementT *, const String &, Vector<String> &,
	RouterT *, const String &, VariableEnvironment &, ErrorHandler *)
{
    assert(0);
    return 0;
}

const ElementTraits *
SynonymElementClassT::find_traits() const
{
    return _eclass->find_traits();
}

RouterT *
SynonymElementClassT::cast_router()
{
    return _eclass->cast_router();
}


RouterT *
ElementClassT::declaration_scope() const
{
    return 0;
}

RouterT *
SynonymElementClassT::declaration_scope() const
{
    return _declaration_scope;
}


ElementClassT *
ElementClassT::overload_type() const
{
    return 0;
}


String
ElementClassT::unparse_signature() const
{
    return name() + "[...]";
}


void
ElementClassT::collect_types(HashMap<ElementClassT *, int> &m) const
{
    m.insert(const_cast<ElementClassT *>(this), 1);
}

void
SynonymElementClassT::collect_types(HashMap<ElementClassT *, int> &m) const
{
    HashMap<ElementClassT *, int>::Pair *p = m.find_pair_force(const_cast<SynonymElementClassT *>(this), 0);
    if (p && p->value == 0) {
	p->value = 1;
	_eclass->collect_types(m);
    }
}


void
ElementClassT::collect_overloads(Vector<ElementClassT *> &v) const
{
    v.push_back(const_cast<ElementClassT *>(this));
}

void
SynonymElementClassT::collect_overloads(Vector<ElementClassT *> &v) const
{
    _eclass->collect_overloads(v);
}


#include <click/hashmap.cc>
