/*
 * Copyright 2016 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the standard MIT license.  See COPYING for more details.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "blktemplate.h"
#include "blkmaker.h"
#include "blkmaker_jansson.h"

static void capabilityname_test() {
	for (unsigned int i = 0; i < GBT_CAPABILITY_COUNT; ++i) {
		const gbt_capabilities_t capid = (1 << i);
		const char * const capname = blktmpl_capabilityname(capid);
		if (!capname) {
			continue;
		}
		const size_t strlen_capname = strlen(capname);
		assert(strlen_capname > 0);
		assert(strlen_capname <= BLKTMPL_LONGEST_CAPABILITY_NAME);
		assert(blktmpl_getcapability(capname) == capid);
	}
}

static void blktxn_test(const int c) {
	struct blktxn_t * const txn = malloc(sizeof(*txn));
	memset(txn, c, sizeof(*txn));
	blktxn_init(txn);
	blktxn_clean(txn);
	free(txn);
}

static bool caps_includes(const uint32_t caps, const uint32_t expected_caps) {
	return (caps & expected_caps) == expected_caps;
}

static void blktmpl_test() {
	blktemplate_t * const tmpl = blktmpl_create();
	
	{
		static const uint32_t expected_fresh_caps = GBT_CBTXN | GBT_WORKID | BMM_TIMEINC | BMM_CBAPPEND | BMM_VERFORCE | BMM_VERDROP | BMAb_COINBASE | BMAb_TRUNCATE;
		assert(caps_includes(blktmpl_addcaps(tmpl), expected_fresh_caps));
	}
	
	assert(!tmpl->version);
	assert(!blktmpl_get_longpoll(tmpl));
	assert(!blktmpl_get_submitold(tmpl));
	
	blktmpl_free(tmpl);
}

static bool json_are_equal(json_t * const ja, json_t * const jb) {
	char *sa, *sb;
	sa = json_dumps(ja, JSON_COMPACT | JSON_SORT_KEYS);
	sb = json_dumps(jb, JSON_COMPACT | JSON_SORT_KEYS);
	const bool rv = !strcmp(sa, sb);
	free(sa);
	free(sb);
	return rv;
}

static void rulecompare(json_t * const jb, const char * const * const rulelist) {
	const size_t z = json_array_size(jb);
	const char *sa;
	json_t *jc;
	
	for (size_t i = 0; i < z; ++i) {
		assert((jc = json_array_get(jb, i)));
		assert((sa = json_string_value(jc)));
		assert(!strcmp(sa, rulelist[i]));
	}
	assert(!rulelist[z]);
}

static void check_request(json_t * const ja, const char * const * const rulelist, uint32_t * const out_caps) {
	const char *sa;
	json_t *jb, *jc;
	
	assert(json_object_get(ja, "id"));
	assert((jb = json_object_get(ja, "method")));
	assert((sa = json_string_value(jb)));
	assert(!strcmp(sa, "getblocktemplate"));
	assert((jb = json_object_get(ja, "params")));
	assert(json_is_array(jb));
	assert(json_array_size(jb) >= 1);
	jc = json_array_get(jb, 0);
	assert(json_is_object(jc));
	assert((jb = json_object_get(jc, "maxversion")));
	assert(json_number_value(jb) == BLKMAKER_MAX_BLOCK_VERSION);
	assert((jb = json_object_get(jc, "rules")));
	assert(json_is_array(jb));
	rulecompare(jb, rulelist);
	if (out_caps) {
		*out_caps = 0;
		if ((jb = json_object_get(jc, "capabilities")) && json_is_array(jb)) {
			const size_t z = json_array_size(jb);
			for (size_t i = 0; i < z; ++i) {
				assert((jc = json_array_get(jb, i)));
				assert((sa = json_string_value(jc)));
				uint32_t capid = blktmpl_getcapability(sa);
				assert(capid);
				*out_caps |= capid;
			}
		}
	}
}

static void blktmpl_request_jansson_test_old() {
	blktemplate_t * const tmpl = blktmpl_create();
	json_t *ja, *jb;
	
	ja = blktmpl_request_jansson2(0, NULL, blkmk_supported_rules);
	jb = blktmpl_request_jansson(0, NULL);
	assert(json_are_equal(ja, jb));
	json_decref(jb);
	
	check_request(ja, blkmk_supported_rules, NULL);
	
	json_decref(ja);
	blktmpl_free(tmpl);
}

static void blktmpl_request_jansson_test_custom_rulelist() {
	blktemplate_t * const tmpl = blktmpl_create();
	json_t *ja;
	const char *custom_rulelist[] = {
		"abc",
		"xyz",
		NULL
	};
	
	ja = blktmpl_request_jansson2(0, NULL, custom_rulelist);
	check_request(ja, custom_rulelist, NULL);
	
	json_decref(ja);
	blktmpl_free(tmpl);
}

static void blktmpl_request_jansson_test_custom_caps_i(json_t * const ja, const uint32_t test_caps) {
	uint32_t caps;
	check_request(ja, blkmk_supported_rules, &caps);
	assert(caps == test_caps);
	json_decref(ja);
}

static void blktmpl_request_jansson_test_custom_caps() {
	blktemplate_t * const tmpl = blktmpl_create();
	json_t *ja;
	uint32_t test_caps = GBT_SERVICE | GBT_LONGPOLL;
	
	ja = blktmpl_request_jansson2(test_caps, NULL, blkmk_supported_rules);
	blktmpl_request_jansson_test_custom_caps_i(ja, test_caps);
	
	test_caps |= blktmpl_addcaps(tmpl);
	ja = blktmpl_request_jansson2(test_caps, NULL, blkmk_supported_rules);
	blktmpl_request_jansson_test_custom_caps_i(ja, test_caps);
	
	blktmpl_free(tmpl);
}

static void blktmpl_request_jansson_test_longpoll() {
	blktemplate_t * const tmpl = blktmpl_create();
	static const char * const lpid = "mylpid00";
	const char *sa;
	json_t *ja, *jb, *jc;
	
	ja = blktmpl_request_jansson2(0, lpid, blkmk_supported_rules);
	check_request(ja, blkmk_supported_rules, NULL);
	
	jb = json_array_get(json_object_get(ja, "params"), 0);
	assert((jc = json_object_get(jb, "longpollid")));
	assert((sa = json_string_value(jc)));
	assert(!strcmp(sa, lpid));
	
	json_decref(ja);
	blktmpl_free(tmpl);
}

static const char *blktmpl_add_jansson_str(blktemplate_t * const tmpl, const char * const s, const time_t time_rcvd) {
	json_t * const j = json_loads(s, 0, NULL);
	assert(j);
	const char * const rv = blktmpl_add_jansson(tmpl, j, time_rcvd);
	json_decref(j);
	return rv;
}

static const time_t simple_time_rcvd = 0x777;

static void blktmpl_jansson_simple() {
	blktemplate_t * const tmpl = blktmpl_create();
	
	assert(!blktmpl_add_jansson_str(tmpl, "{\"version\":2,\"height\":3,\"bits\":\"1d00ffff\",\"curtime\":777,\"previousblockhash\":\"0000000077777777777777777777777777777777777777777777777777777777\",\"coinbasevalue\":512}", simple_time_rcvd));
	assert(tmpl->version == 2);
	assert(tmpl->height == 3);
	assert(!memcmp(tmpl->diffbits, "\xff\xff\0\x1d", 4));
	assert(tmpl->curtime == 777);
	for (int i = 0; i < 7; ++i) {
		assert(tmpl->prevblk[i] == 0x77777777);
	}
	assert(!tmpl->prevblk[7]);
	assert(tmpl->cbvalue == 512);
	
	// Check clear values
	assert(tmpl->txncount == 0);
	assert(tmpl->txns_datasz == 0);
	assert(tmpl->txns_sigops == 0);
	assert(!tmpl->cbtxn);
	assert(!tmpl->workid);
	assert(!blktmpl_get_longpoll(tmpl));
	assert(blktmpl_get_submitold(tmpl));
	assert(!tmpl->target);
	assert(!tmpl->mutations);
	assert(tmpl->aux_count == 0);
	assert(!tmpl->rules);
	assert(!tmpl->unsupported_rule);
	assert(!tmpl->vbavailable);
	assert(!tmpl->vbrequired);
	
	// Check reasonable default ranges
	assert(tmpl->sigoplimit >= 20000);
	assert(tmpl->sizelimit >= 1000000);
	assert(tmpl->expires >= 60);
	assert(tmpl->maxtime >= tmpl->curtime + 60);
	assert(tmpl->maxtimeoff >= 60);
	assert(tmpl->mintime <= tmpl->curtime - 60);
	assert(tmpl->mintimeoff <= -60);
	
	blktmpl_free(tmpl);
}

static void blktmpl_jansson_bip22_required() {
	blktemplate_t * const tmpl = blktmpl_create();
	
	assert(!blktmpl_add_jansson_str(tmpl, "{\"version\":3,\"height\":4,\"bits\":\"1d007fff\",\"curtime\":877,\"previousblockhash\":\"00000000a7777777a7777777a7777777a7777777a7777777a7777777a7777777\",\"coinbasevalue\":640,\"sigoplimit\":100,\"sizelimit\":1000,\"transactions\":[{\"data\":\"01000000019999999999999999999999999999999999999999999999999999999999999999aaaaaaaa00222222220100100000015100000000\",\"required\":true},{\"hash\":\"8eda1a8b67996401a89af8de4edd6715c23a7fb213f9866e18ab9d4367017e8d\",\"data\":\"01000000011c69f212e62f2cdd80937c9c0857cedec005b11d3b902d21007c932c1c7cd20f0000000000444444440100100000015100000000\",\"depends\":[1],\"fee\":12,\"required\":false,\"sigops\":4},{\"data\":\"01000000010099999999999999999999999999999999999999999999999999999999999999aaaaaaaa00555555550100100000015100000000\"}],\"coinbaseaux\":{\"dummy\":\"deadbeef\"},\"coinbasetxn\":{\"data\":\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff07010404deadbeef333333330100100000015100000000\"},\"workid\":\"mywork\"}", simple_time_rcvd));
	assert(tmpl->version == 3);
	assert(tmpl->height == 4);
	assert(!memcmp(tmpl->diffbits, "\xff\x7f\0\x1d", 4));
	assert(tmpl->curtime == 877);
	for (int i = 0; i < 7; ++i) {
		assert(tmpl->prevblk[i] == 0xa7777777);
	}
	assert(!tmpl->prevblk[7]);
	assert(tmpl->cbvalue == 640);
	assert(tmpl->sigoplimit == 100);
	assert(tmpl->sizelimit == 1000);
	assert(tmpl->txncount == 3);
	assert(tmpl->txns);
	assert(tmpl->txns[0].data);
	assert(tmpl->txns[0].datasz == 57);
	assert(!memcmp(tmpl->txns[0].data, "\x01\0\0\0\x01\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\xaa\xaa\xaa\xaa\0\x22\x22\x22\x22\x01\0\x10\0\0\x01\x51\0\0\0\0", 57));
	assert(tmpl->txns[0].dependscount == -1);
	assert(tmpl->txns[0].fee_ == -1);
	assert(tmpl->txns[0].required);
	assert(tmpl->txns[0].sigops_ == -1);
	assert(tmpl->txns[1].data);
	assert(tmpl->txns[1].datasz == 57);
	assert(!memcmp(tmpl->txns[1].data, "\x01\0\0\0\x01\x1c\x69\xf2\x12\xe6\x2f\x2c\xdd\x80\x93\x7c\x9c\x08\x57\xce\xde\xc0\x05\xb1\x1d\x3b\x90\x2d\x21\0\x7c\x93\x2c\x1c\x7c\xd2\x0f\0\0\0\0\0\x44\x44\x44\x44\x01\0\x10\0\0\x01\x51\0\0\0\0", 57));
	assert(tmpl->txns[1].dependscount == 1);
	assert(tmpl->txns[1].depends);
	assert(tmpl->txns[1].depends[0] == 1);
	assert(tmpl->txns[1].fee_ == 12);
	assert(!tmpl->txns[1].required);
	assert(tmpl->txns[1].sigops_ == 4);
	assert(!memcmp(tmpl->txns[1].hash_, "\x8d\x7e\x01\x67\x43\x9d\xab\x18\x6e\x86\xf9\x13\xb2\x7f\x3a\xc2\x15\x67\xdd\x4e\xde\xf8\x9a\xa8\x01\x64\x99\x67\x8b\x1a\xda\x8e", 32));
	assert(tmpl->txns[2].data);
	assert(tmpl->txns[2].datasz == 57);
	assert(!memcmp(tmpl->txns[2].data, "\x01\0\0\0\x01\0\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\x99\xaa\xaa\xaa\xaa\0\x55\x55\x55\x55\x01\0\x10\0\0\x01\x51\0\0\0\0", 57));
	assert(tmpl->txns[2].dependscount == -1);
	assert(tmpl->txns[2].fee_ == -1);
	assert(!tmpl->txns[2].required);
	assert(tmpl->txns[2].sigops_ == -1);
	assert(tmpl->cbtxn->data);
	assert(tmpl->cbtxn->datasz == 64);
	assert(!memcmp(tmpl->cbtxn->data, "\x01\0\0\0\x01\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\xff\xff\xff\xff\x07\x01\x04\x04\xde\xad\xbe\xef\x33\x33\x33\x33\x01\0\x10\0\0\x01\x51\0\0\0\0", 64));
	assert(tmpl->aux_count == 1);
	assert(tmpl->auxs);
	assert(tmpl->auxs[0].auxname);
	assert(!strcmp(tmpl->auxs[0].auxname, "dummy"));
	assert(tmpl->auxs[0].datasz == 4);
	assert(!memcmp(tmpl->auxs[0].data, "\xde\xad\xbe\xef", 4));
	assert(tmpl->workid);
	assert(!strcmp(tmpl->workid, "mywork"));
	assert(blktmpl_get_submitold(tmpl));
	
	blktmpl_free(tmpl);
}

static void blktmpl_jansson_bip22_longpoll() {
	blktemplate_t *tmpl = blktmpl_create();
	const struct blktmpl_longpoll_req *lp;
	
	assert(!blktmpl_get_longpoll(tmpl));
	
	assert(!blktmpl_add_jansson_str(tmpl, "{\"version\":3,\"height\":4,\"bits\":\"1d007fff\",\"curtime\":877,\"previousblockhash\":\"00000000a7777777a7777777a7777777a7777777a7777777a7777777a7777777\",\"coinbasevalue\":640,\"longpollid\":\"mylpid\"}", simple_time_rcvd));
	lp = blktmpl_get_longpoll(tmpl);
	assert(lp->id);
	assert(!strcmp(lp->id, "mylpid"));
	assert(!lp->uri);
	assert(blktmpl_get_submitold(tmpl));
	
	blktmpl_free(tmpl);
	tmpl = blktmpl_create();
	
	assert(!blktmpl_add_jansson_str(tmpl, "{\"version\":3,\"height\":4,\"bits\":\"1d007fff\",\"curtime\":877,\"previousblockhash\":\"00000000a7777777a7777777a7777777a7777777a7777777a7777777a7777777\",\"coinbasevalue\":640,\"longpollid\":\"myLPid\",\"longpolluri\":\"/LP\",\"submitold\":false}", simple_time_rcvd));
	lp = blktmpl_get_longpoll(tmpl);
	assert(lp->id);
	assert(!strcmp(lp->id, "myLPid"));
	assert(lp->uri);
	assert(!strcmp(lp->uri, "/LP"));
	assert(!blktmpl_get_submitold(tmpl));
	
	blktmpl_free(tmpl);
}

static void blktmpl_jansson_bip23_bpe() {
	blktemplate_t *tmpl = blktmpl_create();
	
	assert(!blktmpl_add_jansson_str(tmpl, "{\"version\":3,\"height\":4,\"bits\":\"1d007fff\",\"curtime\":877,\"previousblockhash\":\"00000000a7777777a7777777a7777777a7777777a7777777a7777777a7777777\",\"coinbasevalue\":640,\"expires\":99,\"target\":\"0000000077777777777777777777777777777777777777777777777777777777\"}", simple_time_rcvd));
	assert(tmpl->expires == 99);
	assert(!(*tmpl->target)[0]);
	for (int i = 1; i < 8; ++i) {
		assert((*tmpl->target)[i] == 0x77777777);
	}
	
	blktmpl_free(tmpl);
}

static void blktmpl_jansson_bip23_mutations() {
	blktemplate_t *tmpl = blktmpl_create();
	
	assert(!blktmpl_add_jansson_str(tmpl, "{\"version\":3,\"height\":4,\"bits\":\"1d007fff\",\"curtime\":877,\"previousblockhash\":\"00000000a7777777a7777777a7777777a7777777a7777777a7777777a7777777\",\"coinbasevalue\":640,\"maxtime\":2113929216,\"maxtimeoff\":50,\"mintime\":800,\"mintimeoff\":-50,\"mutable\":[\"prevblock\",\"version/force\"],\"noncerange\":\"01000000f0000000\"}", simple_time_rcvd));
	assert(tmpl->maxtime == 2113929216);
	assert(tmpl->maxtimeoff == 50);
	assert(tmpl->mintime == 800);
	assert(tmpl->mintimeoff == -50);
	// As of right now, implied mutations are not included in the value
	// assert(tmpl->mutations == (BMM_CBAPPEND | BMM_CBSET | BMM_GENERATE | BMM_TIMEINC | BMM_TIMEDEC | BMM_TXNADD | BMM_PREVBLK | BMM_VERFORCE));
	assert(caps_includes(tmpl->mutations, BMM_PREVBLK | BMM_VERFORCE));
	
	blktmpl_free(tmpl);
	tmpl = blktmpl_create();
	
	assert(!blktmpl_add_jansson_str(tmpl, "{\"version\":3,\"height\":4,\"bits\":\"1d007fff\",\"curtime\":877,\"previousblockhash\":\"00000000a7777777a7777777a7777777a7777777a7777777a7777777a7777777\",\"coinbasevalue\":640,\"mutable\":[\"version/reduce\",\"coinbase/append\",\"generation\",\"time\",\"transactions\"],\"coinbasetxn\":{\"data\":\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff07010404deadbeef333333330100100000015100000000\"},\"transactions\":[]}", simple_time_rcvd));
	assert(tmpl->mutations == (BMM_CBAPPEND | BMM_GENERATE | BMM_TIMEINC | BMM_TIMEDEC | BMM_TXNADD | BMM_VERDROP));
	
	blktmpl_free(tmpl);
}

static void blktmpl_jansson_bip23_abbrev() {
	blktemplate_t * const tmpl = blktmpl_create();
	
	assert(!blktmpl_add_jansson_str(tmpl, "{\"version\":3,\"height\":4,\"bits\":\"1d007fff\",\"curtime\":877,\"previousblockhash\":\"00000000a7777777a7777777a7777777a7777777a7777777a7777777a7777777\",\"coinbasevalue\":640,\"mutable\":[\"submit/hash\",\"submit/coinbase\",\"submit/truncate\"]}", simple_time_rcvd));
	assert(tmpl->mutations == (BMA_TXNHASH | BMAb_COINBASE | BMAb_TRUNCATE));
	
	blktmpl_free(tmpl);
}

static void blktmpl_jansson_bip9() {
	blktemplate_t *tmpl;
	
	tmpl = blktmpl_create();
	assert(!blktmpl_add_jansson_str(tmpl, "{\"version\":536871040,\"height\":4,\"bits\":\"1d007fff\",\"curtime\":877,\"previousblockhash\":\"00000000a7777777a7777777a7777777a7777777a7777777a7777777a7777777\",\"coinbasevalue\":640,\"rules\":[\"csv\"],\"vbavailable\":{\"!segwit\":7}}", simple_time_rcvd));
	assert(tmpl->version == 0x20000080);
	assert(tmpl->rules);
	assert(tmpl->rules[0]);
	assert(!strcmp(tmpl->rules[0], "csv"));
	assert(!tmpl->rules[1]);
	assert(!tmpl->unsupported_rule);
	assert(tmpl->vbavailable);
	assert(tmpl->vbavailable[0]);
	assert(tmpl->vbavailable[0]->name);
	assert(!strcmp(tmpl->vbavailable[0]->name, "!segwit"));
	assert(tmpl->vbavailable[0]->bitnum == 7);
	assert(!tmpl->vbavailable[1]);
	assert(!tmpl->vbrequired);
	
	blktmpl_free(tmpl);
	tmpl = blktmpl_create();
	
	assert(!blktmpl_add_jansson_str(tmpl, "{\"version\":536871040,\"height\":4,\"bits\":\"1d007fff\",\"curtime\":877,\"previousblockhash\":\"00000000a7777777a7777777a7777777a7777777a7777777a7777777a7777777\",\"coinbasevalue\":640,\"rules\":[\"csv\"],\"vbavailable\":{\"!segwit\":7},\"vbrequired\":128}", simple_time_rcvd));
	assert(tmpl->version == 0x20000080);
	assert(tmpl->rules);
	assert(tmpl->rules[0]);
	assert(!strcmp(tmpl->rules[0], "csv"));
	assert(!tmpl->rules[1]);
	assert(!tmpl->unsupported_rule);
	assert(tmpl->vbavailable);
	assert(tmpl->vbavailable[0]);
	assert(tmpl->vbavailable[0]->name);
	assert(!strcmp(tmpl->vbavailable[0]->name, "!segwit"));
	assert(tmpl->vbavailable[0]->bitnum == 7);
	assert(!tmpl->vbavailable[1]);
	assert(tmpl->vbrequired == 0x80);
	
	blktmpl_free(tmpl);
	tmpl = blktmpl_create();
	
	assert(!blktmpl_add_jansson_str(tmpl, "{\"version\":536871040,\"height\":4,\"bits\":\"1d007fff\",\"curtime\":877,\"previousblockhash\":\"00000000a7777777a7777777a7777777a7777777a7777777a7777777a7777777\",\"coinbasevalue\":640,\"rules\":[\"csv\",\"foo\"],\"vbavailable\":{}}", simple_time_rcvd));
	assert(tmpl->version == 0x20000080);
	assert(tmpl->rules);
	assert(tmpl->rules[0]);
	assert(!strcmp(tmpl->rules[0], "csv"));
	assert(tmpl->rules[1]);
	assert(!strcmp(tmpl->rules[1], "foo"));
	assert(!tmpl->rules[2]);
	assert(tmpl->unsupported_rule);
	assert(tmpl->vbavailable);
	assert(!tmpl->vbavailable[0]);
	assert(!tmpl->vbrequired);
	
	blktmpl_free(tmpl);
	tmpl = blktmpl_create();
	
	assert(blktmpl_add_jansson_str(tmpl, "{\"version\":536871040,\"height\":4,\"bits\":\"1d007fff\",\"curtime\":877,\"previousblockhash\":\"00000000a7777777a7777777a7777777a7777777a7777777a7777777a7777777\",\"coinbasevalue\":640,\"rules\":[\"csv\",\"!foo\"],\"vbavailable\":{}}", simple_time_rcvd));
	
	blktmpl_free(tmpl);
}

int main() {
	puts("capabilityname");
	capabilityname_test();
	
	puts("blktxn");
	blktxn_test('\0');
	blktxn_test('\xa5');
	blktxn_test('\xff');
	
	puts("blktmpl");
	blktmpl_test();
	
	puts("blktmpl_request_jansson");
	blktmpl_request_jansson_test_old();
	blktmpl_request_jansson_test_custom_rulelist();
	blktmpl_request_jansson_test_custom_caps();
	blktmpl_request_jansson_test_longpoll();
	
	puts("blktmpl_jansson");
	blktmpl_jansson_simple();
	blktmpl_jansson_bip22_required();
	blktmpl_jansson_bip22_longpoll();
	blktmpl_jansson_bip23_bpe();
	blktmpl_jansson_bip23_mutations();
	blktmpl_jansson_bip23_abbrev();
	blktmpl_jansson_bip9();
}
