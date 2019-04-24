#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "jsmn_assignment.h"
#include "test_json.h"
// #include "../libnetdata.h"

#define JSON_TOKENS 1024

int json_tokens = JSON_TOKENS;

jsmntok_t *json_tokenise(char *js, size_t len, size_t *count)
{
	int n = json_tokens;
	if(!js || !len) {
		// error("JSON: json string is empty.");
		return NULL;
	}

	jsmn_parser parser;
	jsmn_init(&parser);

	jsmntok_t *tokens = malloc(sizeof(jsmntok_t) * n);
	if(!tokens) return NULL;

	int ret = jsmn_parse(&parser, js, len, tokens, n);
	while (ret == JSMN_ERROR_NOMEM) {
		n *= 2;
		jsmntok_t *new = realloc(tokens, sizeof(jsmntok_t) * n);
		if(!new) {
			free(tokens);
			return NULL;
		}
		tokens = new;
		ret = jsmn_parse(&parser, js, len, tokens, n);
	}

	if (ret == JSMN_ERROR_INVAL) {
		// error("JSON: Invalid json string.");
		free(tokens);
		return NULL;
	}
	else if (ret == JSMN_ERROR_PART) {
		// error("JSON: Truncated JSON string.");
		free(tokens);
		return NULL;
	}

	if(count) *count = (size_t)ret;

	if(json_tokens < n) json_tokens = n;
	return tokens;
}

int json_callback_print(JSON_ENTRY *e)
{
    BUFFER *wb=buffer_create(300);

	buffer_sprintf(wb,"%s = ", e->name);
    char txt[50];
	switch(e->type) {
		case JSON_OBJECT:
			e->callback_function = json_callback_print;
			buffer_strcat(wb,"OBJECT");
			break;

		case JSON_ARRAY:
			e->callback_function = json_callback_print;
			sprintf(txt,"ARRAY[%lu]", e->data.items);
			buffer_strcat(wb, txt);
			break;

		case JSON_STRING:
			buffer_strcat(wb, e->data.string);
			break;

		case JSON_NUMBER:
            sprintf(txt,"%Lf", e->data.number);
			buffer_strcat(wb,txt);

			break;

		case JSON_BOOLEAN:
			buffer_strcat(wb, e->data.boolean?"TRUE":"FALSE");
			break;

		case JSON_NULL:
			buffer_strcat(wb,"NULL");
			break;
	}
    info("JSON: %s", buffer_tostring(wb));
	buffer_free(wb);
	return 0;
}

size_t json_walk_string(char *js, jsmntok_t *t, size_t start, JSON_ENTRY *e)
{
	char old = js[t[start].end];
	js[t[start].end] = '\0';
	e->original_string = &js[t[start].start];

	e->type = JSON_STRING;
	e->data.string = e->original_string;
	if(e->callback_function) e->callback_function(e);
	js[t[start].end] = old;
	return 1;
}

size_t json_walk_primitive(char *js, jsmntok_t *t, size_t start, JSON_ENTRY *e)
{
	char old = js[t[start].end];
	js[t[start].end] = '\0';
	e->original_string = &js[t[start].start];

	switch(e->original_string[0]) {
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
		case '8': case '9': case '-': case '.':
			e->type = JSON_NUMBER;
			e->data.number = strtold(e->original_string, NULL);
			break;

		case 't': case 'T':
			e->type = JSON_BOOLEAN;
			e->data.boolean = 1;
			break;

		case 'f': case 'F':
			e->type = JSON_BOOLEAN;
			e->data.boolean = 0;
			break;

		case 'n': case 'N':
		default:
			e->type = JSON_NULL;
			break;
	}
	if(e->callback_function) e->callback_function(e);
	js[t[start].end] = old;
	return 1;
}

size_t json_walk_array(char *js, jsmntok_t *t, size_t nest, size_t start, JSON_ENTRY *e)
{
	JSON_ENTRY ne = {
			.name = "",
			.fullname = "",
			.callback_data = NULL,
			.callback_function = NULL
	};

	char old = js[t[start].end];
	js[t[start].end] = '\0';
	ne.original_string = &js[t[start].start];

	memcpy(&ne, e, sizeof(JSON_ENTRY));
	ne.type = JSON_ARRAY;
	ne.data.items = t[start].size;
	ne.callback_function = NULL;
	ne.name[0]='\0';
	ne.fullname[0]='\0';
	if(e->callback_function) e->callback_function(&ne);
	js[t[start].end] = old;

	size_t i, init = start, size = t[start].size;

	start++;
	for(i = 0; i < size ; i++) {
		ne.pos = i;
		if (!e->name || !e->fullname || strlen(e->name) > JSON_NAME_LEN  - 24 || strlen(e->fullname) > JSON_FULLNAME_LEN -24) {
		    info("JSON: JSON walk_array ignoring element with name:%s fullname:%s",e->name, e->fullname);
		    continue;
		}
		sprintf(ne.name, "%s[%lu]", e->name, i);
		sprintf(ne.fullname, "%s[%lu]", e->fullname, i);

		switch(t[start].type) {
			case JSMN_PRIMITIVE:
				start += json_walk_primitive(js, t, start, &ne);
				break;

			case JSMN_OBJECT:
				start += json_walk_object(js, t, nest + 1, start, &ne);
				break;

			case JSMN_ARRAY:
				start += json_walk_array(js, t, nest + 1, start, &ne);
				break;

			case JSMN_STRING:
				start += json_walk_string(js, t, start, &ne);
				break;
		}
	}
	return start - init;
}

size_t json_walk_object(char *js, jsmntok_t *t, size_t nest, size_t start, JSON_ENTRY *e)
{
	JSON_ENTRY ne = {
		.name = "",
		.fullname = "",
		.callback_data = NULL,
		.callback_function = NULL
	};

	char old = js[t[start].end];
	js[t[start].end] = '\0';
	ne.original_string = &js[t[start].start];
	memcpy(&ne, e, sizeof(JSON_ENTRY));
	ne.type = JSON_OBJECT;
	ne.callback_function = NULL;
	if(e->callback_function) e->callback_function(&ne);
	js[t[start].end] = old;

	int key = 1;
	size_t i, init = start, size = t[start].size;

	start++;
	for(i = 0; i < size ; i++) {
		switch(t[start].type) {
			case JSMN_PRIMITIVE:
				start += json_walk_primitive(js, t, start, &ne);
				key = 1;
				break;

			case JSMN_OBJECT:
				start += json_walk_object(js, t, nest + 1, start, &ne);
				key = 1;
				break;

			case JSMN_ARRAY:
				start += json_walk_array(js, t, nest + 1, start, &ne);
				key = 1;
				break;

			case JSMN_STRING:
			default:
				if(key) {
					int len = t[start].end - t[start].start;
					if (unlikely(len>JSON_NAME_LEN)) len=JSON_NAME_LEN;
					strncpy(ne.name, &js[t[start].start], len);
					ne.name[len] = '\0';
					len=strlen(e->fullname) + strlen(e->fullname[0]?".":"") + strlen(ne.name);
					char *c = mallocz((len+1)*sizeof(char));
					sprintf(c,"%s%s%s", e->fullname, e->fullname[0]?".":"", ne.name);
					if (unlikely(len>JSON_FULLNAME_LEN)) len=JSON_FULLNAME_LEN;
					strncpy(ne.fullname, c, len);
					freez(c);
					start++;
					key = 0;
				}
				else {
					start += json_walk_string(js, t, start, &ne);
					key = 1;
				}
				break;
		}
	}
	return start - init;
}

size_t json_walk_tree(char *js, jsmntok_t *t, void *callback_data, int (*callback_function)(struct json_entry *))
{
	JSON_ENTRY e = {
		.name = "",
		.fullname = "",
		.callback_data = callback_data,
		.callback_function = callback_function
	};

	switch (t[0].type) {
		case JSMN_OBJECT:
			e.type = JSON_OBJECT;
			json_walk_object(js, t, 0, 0, &e);
			break;

		case JSMN_ARRAY:
			e.type = JSON_ARRAY;
			json_walk_array(js, t, 0, 0, &e);
			break;

		case JSMN_PRIMITIVE:
		case JSMN_STRING:
			break;
	}
	return 1;
}

int json_parse(char *js, void *callback_data, int (*callback_function)(JSON_ENTRY *))
{
	size_t count;
	if(js) {
		jsmntok_t *tokens = json_tokenise(js, strlen(js), &count);
		if(tokens) {
			json_walk_tree(js, tokens, callback_data, callback_function);
			freez(tokens);
			return JSON_OK;
		}

		return JSON_CANNOT_PARSE;
	}
	return JSON_CANNOT_DOWNLOAD;
}

int json_test(char *str)
{
	return json_parse(str, NULL, json_callback_print);
}
