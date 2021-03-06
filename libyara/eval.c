/*
Copyright (c) 2007. Victor M. Alvarez [plusvic@gmail.com].

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "yara.h"
#include "ast.h"
#include "eval.h"
#include "regex.h"

#include <string.h>

#define UNDEFINED           0xFABADAFABADALL
#define IS_UNDEFINED(x)     ((x) == UNDEFINED)

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef char int8;
typedef short int16;
typedef int int32;

#define function_read(type, tsize) long long read_##type##tsize(MEMORY_BLOCK* block, size_t offset) \
{ \
    while (block != NULL) \
    { \
        if (offset >= block->base && \
            block->size >= tsize/8 && \
	    offset < block->base + block->size - (tsize/8 - 1)) \
        { \
            return *((type##tsize *) (block->data + offset - block->base)); \
        } \
        block = block->next; \
    } \
    return UNDEFINED; \
};

#define COMPARISON_OPERATOR(operator, term, context) \
    op1 = evaluate(term->op1, context); \
    op2 = evaluate(term->op2, context); \
    if (IS_UNDEFINED(op1) || IS_UNDEFINED(op2)) \
        return FALSE; \
    else \
        return op1 operator op2;\

#define ARITHMETIC_OPERATOR(operator, term, context) \
    op1 = evaluate(term->op1, context); \
    op2 = evaluate(term->op2, context); \
    if (IS_UNDEFINED(op1) || IS_UNDEFINED(op2)) \
        return UNDEFINED; \
    else \
        return op1 operator op2;\


function_read(uint, 8)
function_read(uint, 16)
function_read(uint, 32)
function_read(int, 8)
function_read(int, 16)
function_read(int, 32)



long long evaluate(TERM* term, EVALUATION_CONTEXT* context)
{
	size_t offs, hi_bound, lo_bound;
    long long op1;
    long long op2;
    long long index;
	unsigned int i;
	unsigned int needed;
	unsigned int satisfied;
	int ovector[3];
	int rc;
  int len;

    STRING* string;
    STRING* saved_anonymous_string;

	TERM_CONST* term_const = ((TERM_CONST*) term);
	TERM_UNARY_OPERATION* term_unary = ((TERM_UNARY_OPERATION*) term);
	TERM_BINARY_OPERATION* term_binary = ((TERM_BINARY_OPERATION*) term);
	TERM_TERNARY_OPERATION* term_ternary = ((TERM_TERNARY_OPERATION*) term);
	TERM_STRING* term_string = ((TERM_STRING*) term);
	TERM_VARIABLE* term_variable = ((TERM_VARIABLE*) term);
	TERM_STRING_OPERATION* term_string_operation = ((TERM_STRING_OPERATION*) term);

    TERM_INTEGER_FOR* term_integer_for;

	MATCH* match;
    TERM* item;
    TERM_RANGE* range;
    TERM_ITERABLE* items;
	TERM_STRING* t;

	switch(term->type)
	{
	case TERM_TYPE_CONST:
		return term_const->value;

	case TERM_TYPE_FILESIZE:
		return context->file_size;

	case TERM_TYPE_ENTRYPOINT:
		return context->entry_point;

	case TERM_TYPE_RULE:
		return evaluate(term_binary->op1, context);

	case TERM_TYPE_STRING:

	    if (term_string->string == NULL) /* it's an anonymous string */
	    {
            string = context->current_string;
	    }
	    else
	    {
            string = term_string->string;
	    }

		return string->flags & STRING_FLAGS_FOUND;

	case TERM_TYPE_STRING_AT:

    	if (term_string->string == NULL) /* it's an anonymous string */
        {
            string = context->current_string;
        }
        else
        {
            string = term_string->string;
        }

		if (string->flags & STRING_FLAGS_FOUND)
		{
			offs = evaluate(term_string->offset, context);

			match = string->matches_head;

			while (match != NULL)
			{
				if (match->offset == offs)
					return 1;

				match = match->next;
			}

			return 0;
		}
		else return 0;

	case TERM_TYPE_STRING_IN_RANGE:

        if (term_string->string == NULL) /* it's an anonymous string */
        {
            string = context->current_string;
        }
        else
        {
            string = term_string->string;
        }

		if (string->flags & STRING_FLAGS_FOUND)
		{
            range = (TERM_RANGE*) term_string->range;

			lo_bound = evaluate(range->min, context);
			hi_bound = evaluate(range->max, context);

			if (IS_UNDEFINED(lo_bound) || IS_UNDEFINED(hi_bound))
			{
				return 0;
			}

			match = string->matches_head;

			while (match != NULL)
			{
				if (match->offset >= lo_bound && match->offset <= hi_bound)
					return 1;

				match = match->next;
			}

			return 0;
		}
		else return 0;

	case TERM_TYPE_STRING_IN_SECTION_BY_NAME:
		return 0; /*TODO: Implementar section by name*/

	case TERM_TYPE_STRING_COUNT:

		i = 0;

		if (term_string->string == NULL) /* it's an anonymous string */
        {
            string = context->current_string;
        }
        else
        {
            string = term_string->string;
        }

		match = string->matches_head;

		while (match != NULL)
		{
			i++;
			match = match->next;
		}

		return i;

	case TERM_TYPE_STRING_OFFSET:

        i = 1;
	    index = evaluate(term_string->index, context);

    	if (term_string->string == NULL) /* it's an anonymous string */
        {
            string = context->current_string;
        }
        else
        {
            string = term_string->string;
        }

        match = string->matches_head;

		while (match != NULL && i < index)
		{
			match = match->next;
            i++;
		}

		if (match != NULL && i == index)
		{
		    return match->offset;
		}

        return UNDEFINED;


	case TERM_TYPE_AND:

	    if (evaluate(term_binary->op1, context))
		    return evaluate(term_binary->op2, context);
	    else
		    return 0;

	case TERM_TYPE_OR:

		if (evaluate(term_binary->op1, context))
			return 1;
		else
			return evaluate(term_binary->op2, context);

	case TERM_TYPE_NOT:
		return !evaluate(term_unary->op, context);

	case TERM_TYPE_ADD:
	    ARITHMETIC_OPERATOR(+, term_binary, context);

	case TERM_TYPE_SUB:
		ARITHMETIC_OPERATOR(-, term_binary, context);

	case TERM_TYPE_MUL:
		ARITHMETIC_OPERATOR(*, term_binary, context);

	case TERM_TYPE_DIV:
		ARITHMETIC_OPERATOR(/, term_binary, context);

	case TERM_TYPE_MOD:
	    ARITHMETIC_OPERATOR(%, term_binary, context);

	case TERM_TYPE_BITWISE_XOR:
	    ARITHMETIC_OPERATOR(^, term_binary, context);

	case TERM_TYPE_BITWISE_AND:
	    ARITHMETIC_OPERATOR(&, term_binary, context);

	case TERM_TYPE_BITWISE_OR:
    	ARITHMETIC_OPERATOR(|, term_binary, context);

	case TERM_TYPE_SHIFT_LEFT:
    	ARITHMETIC_OPERATOR(<<, term_binary, context);

	case TERM_TYPE_SHIFT_RIGHT:
    	ARITHMETIC_OPERATOR(>>, term_binary, context);

    case TERM_TYPE_BITWISE_NOT:

        op1 = evaluate(term_unary->op, context);
        if (IS_UNDEFINED(op1))
            return UNDEFINED;
        else
            return ~op1;

	case TERM_TYPE_GT:
        COMPARISON_OPERATOR(>, term_binary, context);

	case TERM_TYPE_LT:
	    COMPARISON_OPERATOR(<, term_binary, context);

	case TERM_TYPE_GE:
		COMPARISON_OPERATOR(>=, term_binary, context);

	case TERM_TYPE_LE:
		COMPARISON_OPERATOR(<=, term_binary, context);

	case TERM_TYPE_EQ:
		COMPARISON_OPERATOR(==, term_binary, context);

	case TERM_TYPE_NOT_EQ:
		COMPARISON_OPERATOR(!=, term_binary, context);

	case TERM_TYPE_OF:

		t = (TERM_STRING*) term_binary->op2;
		needed = evaluate(term_binary->op1, context);
        satisfied = 0;
        i = 0;

		while (t != NULL)
		{
			if (evaluate((TERM*) t, context))
			{
				satisfied++;
			}

			t = t->next;
            i++;
		}

		if (needed == 0)  /* needed == 0 means ALL*/
            needed = i;

        return (satisfied >= needed);

	case TERM_TYPE_STRING_FOR:

        t = (TERM_STRING*) term_ternary->op2;

		needed = evaluate(term_ternary->op1, context);
        satisfied = 0;
        i = 0;

		while (t != NULL)
		{
            saved_anonymous_string = context->current_string;
            context->current_string = t->string;

			if (evaluate(term_ternary->op3, context))
			{
				satisfied++;
			}

            context->current_string = saved_anonymous_string;

			t = t->next;
            i++;
		}

		if (needed == 0)  /* needed == 0 means ALL*/
            needed = i;

        return (satisfied >= needed);

	case TERM_TYPE_INTEGER_FOR:

        term_integer_for = (TERM_INTEGER_FOR*) term;
        items = term_integer_for->items;

        needed = evaluate(term_integer_for->count, context);
        satisfied = 0;
        i = 0;

        item = items->first(items, evaluate, context);

        while (item != NULL)
        {
            term_integer_for->variable->integer = evaluate(item, context);

            if (evaluate(term_integer_for->expression, context))
			{
				satisfied++;
			}

            item = items->next(items, evaluate, context);
            i++;
        }

        if (needed == 0)  /* needed == 0 means ALL*/
            needed = i;

        return (satisfied >= needed);

    case TERM_TYPE_UINT8_AT_OFFSET:

        return read_uint8(context->mem_block, evaluate(term_unary->op, context));

    case TERM_TYPE_UINT16_AT_OFFSET:

        return read_uint16(context->mem_block, evaluate(term_unary->op, context));

    case TERM_TYPE_UINT32_AT_OFFSET:

        return read_uint32(context->mem_block, evaluate(term_unary->op, context));

    case TERM_TYPE_INT8_AT_OFFSET:

        return read_int8(context->mem_block, evaluate(term_unary->op, context));

    case TERM_TYPE_INT16_AT_OFFSET:

        return read_int16(context->mem_block, evaluate(term_unary->op, context));

    case TERM_TYPE_INT32_AT_OFFSET:

        return read_int32(context->mem_block, evaluate(term_unary->op, context));

    case TERM_TYPE_VARIABLE:

        if (term_variable->variable->type == VARIABLE_TYPE_STRING)
        {
            return ( term_variable->variable->string != NULL && *term_variable->variable->string != '\0');
        }
        else if (term_variable->variable->type == VARIABLE_TYPE_BOOLEAN)
        {
            return term_variable->variable->boolean;
        }
        else
        {
            return term_variable->variable->integer;
        }

    case TERM_TYPE_STRING_MATCH:

        len = strlen(term_string_operation->variable->string);

        if (len == 0)
            return FALSE;

        rc = regex_exec(&(term_string_operation->re),
                        FALSE,
                        term_string_operation->variable->string,
                        len);

        return (rc >= 0);

	case TERM_TYPE_STRING_CONTAINS:

		return (strstr(term_string_operation->variable->string, term_string_operation->string) != NULL);

	default:
		return 0;
	}
}
