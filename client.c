// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

// This should come first.
// Glibc macro to expose definitions corresponding to the POSIX.1-2008 base specification.
// See https://man7.org/linux/man-pages/man7/feature_test_macros.7.html.
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "demi/libos.h"
#include "demi/sga.h"
#include "demi/wait.h"

#include "common.h"

#define DATA_SIZE 64
#define MAX_MSGS  (1024*1024)

#include <sys/types.h>
#include <x86intrin.h>
static inline
time_t read_tsc(void) {
	_mm_lfence();  // optionally wait for earlier insns to retire before reading the clock
	time_t tsc = __rdtsc();
	_mm_lfence();  // optionally block later instructions until rdtsc retires
	return tsc;
}

/*====================================================================================================================*
 * connect_wait()                                                                                                     *
 *====================================================================================================================*/

/**
 * @brief Connects to a remote socket and waits for operation to complete.
 *
 * @param qd    Target queue descriptor.
 * @param saddr Remote socket address.
 */
static void connect_wait(int qd, const struct sockaddr_in *saddr)
{
	demi_qtoken_t qt = -1;
	demi_qresult_t qr = {0};

	/* Connect to remote */
	assert(demi_connect(&qt, qd, (const struct sockaddr *)saddr, sizeof(struct sockaddr_in)) == 0);

	/* Wait for operation to complete. */
	assert(demi_wait(&qr, qt, NULL) == 0);

	/* Parse operation result. */
	assert(qr.qr_opcode == DEMI_OPC_CONNECT);
}

/*====================================================================================================================*
 * push_wait()                                                                                                        *
 *====================================================================================================================*/

/**
 * @brief Pushes a scatter-gather array to a remote socket and waits for operation to complete.
 *
 * @param qd  Target queue descriptor.
 * @param sga Target scatter-gather array.
 * @param qr  Storage location for operation result.
 */
static void push_wait(int qd, demi_sgarray_t *sga, demi_qresult_t *qr)
{
	demi_qtoken_t qt = -1;

	/* Push data. */
	assert(demi_push(&qt, qd, sga) == 0);

	/* Wait push operation to complete. */
	assert(demi_wait(qr, qt, NULL) == 0);

	/* Parse operation result. */
	assert(qr->qr_opcode == DEMI_OPC_PUSH);
}

/*====================================================================================================================*
 * pop_wait()                                                                                                         *
 *====================================================================================================================*/

/**
 * @brief Pops a scatter-gather array and waits for operation to complete.
 *
 * @param qd Target queue descriptor.
 * @param qr Storage location for operation result.
 */
static void pop_wait(int qd, demi_qresult_t *qr)
{
	demi_qtoken_t qt = -1;

	/* Pop data. */
	assert(demi_pop(&qt, qd) == 0);

	/* Wait for pop operation to complete. */
	assert(demi_wait(qr, qt, NULL) == 0);

	/* Parse operation result. */
	assert(qr->qr_opcode == DEMI_OPC_POP);
	assert(qr->qr_value.sga.sga_segs != 0);
}


static void report_measurements(uint64_t *m, size_t count)
{
	printf("-------------------------------------\n");
	for (size_t i = 0; i < count; i++) {
		printf("%ld\n", m[i]);
	}
	printf("-------------------------------------\n");
}

/*====================================================================================================================*
 * client()                                                                                                           *
 *====================================================================================================================*/

/**
 * @brief TCP echo client.
 *
 * @param argc   Argument count.
 * @param argv   Argument list.
 * @param remote Remote socket address.
 * @param data_size Number of bytes in each message.
 * @param max_msgs Maximum number of messages to transfer.
 */
static void client(int argc, char *const argv[], const struct sockaddr_in *remote, size_t data_size, unsigned max_msgs)
{
	size_t nbytes = 0;
	int sockqd = -1;
	size_t max_bytes = data_size * max_msgs;
	uint64_t *measurments = calloc(MAX_MSGS + 100, sizeof(uint64_t));
	size_t m_index = 0;
	time_t before, after;

	/* Initialize demikernel */
	assert(demi_init(argc, argv) == 0);

	/* Setup socket. */
	assert(demi_socket(&sockqd, AF_INET, SOCK_STREAM, 0) == 0);

	/* Connect to server. */
	connect_wait(sockqd, remote);

	/* Run. */
	while (nbytes < max_bytes)
	{
		demi_qresult_t qr = {0};
		demi_sgarray_t sga = {0};

		/* Allocate scatter-gather array. */
		sga = demi_sgaalloc(data_size);
		assert(sga.sga_segs != 0);

		/* Cook data. */
		memset(sga.sga_segs[0].sgaseg_buf, 0xAB, data_size);

		before = read_tsc();
		/* Push scatter-gather array. */
		push_wait(sockqd, &sga, &qr);

		/* Release sent scatter-gather array. */
		assert(demi_sgafree(&sga) == 0);

		/* Pop data scatter-gather array. */
		memset(&qr, 0, sizeof(demi_qresult_t));
		pop_wait(sockqd, &qr);
		after = read_tsc();
		measurments[m_index] = after - before;
		m_index++;

		nbytes += qr.qr_value.sga.sga_segs[0].sgaseg_len;

		/* Release received scatter-gather array. */
		assert(demi_sgafree(&qr.qr_value.sga) == 0);

		/* fprintf(stdout, "pong (%zu)\n", nbytes); */
	}
	report_measurements(measurments, m_index);
}

/*====================================================================================================================*
 * usage()                                                                                                            *
 *====================================================================================================================*/

/**
 * @brief Prints program usage.
 *
 * @param progname Program name.
 */
static void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s ipv4-address port\n", progname);
}

/*====================================================================================================================*
 * build_sockaddr()                                                                                                   *
 *====================================================================================================================*/

/**
 * @brief Builds a socket address.
 *
 * @param ip_str    String representation of an IP address.
 * @param port_str  String representation of a port number.
 * @param addr      Storage location for socket address.
 */
void build_sockaddr(const char *const ip_str, const char *const port_str, struct sockaddr_in *const addr)
{
	int port = -1;

	sscanf(port_str, "%d", &port);
	addr->sin_family = AF_INET;
	addr->sin_port = htons(port);
	assert(inet_pton(AF_INET, ip_str, &addr->sin_addr) == 1);
}

/*====================================================================================================================*
 * main()                                                                                                             *
 *====================================================================================================================*/

int main(int argc, char *const argv[])
{
	if (argc >= 3)
	{
		reg_sighandlers();

		struct sockaddr_in saddr = {0};
		size_t data_size = DATA_SIZE;
		unsigned max_msgs = MAX_MSGS;

		if (argc >= 4)
			sscanf(argv[3], "%zu", &data_size);
		if (argc >= 5)
			sscanf(argv[4], "%u", &max_msgs);

		/* The server that I work with require this space */
		assert (data_size > 16);
		/* Build addresses.*/
		build_sockaddr(argv[1], argv[2], &saddr);

		/* Run. */
		client(argc, argv, &saddr, data_size, max_msgs);

		return (EXIT_SUCCESS);
	}

	usage(argv[0]);

	return (EXIT_SUCCESS);
}
