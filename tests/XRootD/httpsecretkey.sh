#!/usr/bin/env bash

function setup_httpsecretkey() {
	require_commands curl
}

function test_httpsecretkey() {
	export HOST="http://localhost:${XRD_PORT}"

        curl -sS -i --max-time 5 -o /dev/null "${HOST}/foo?xrdhttptk=BBBB&xrdhttptime=$(date +%s)"
        ret=$?
        # return code 52 means empty reply from server
        assert_eq 52 $ret

        # try again to check we get the same response
        # return code 52 means empty reply from server
        curl -sS -i --max-time 5 -o /dev/null "${HOST}/foo?xrdhttptk=BBBB&xrdhttptime=$(date +%s)"
        ret=$?
        assert_eq 52 $ret
}
