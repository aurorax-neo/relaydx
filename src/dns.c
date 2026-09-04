/*
 * Minimal DNS wire-format helpers used by the IPv6 server modules.
 * This avoids a runtime dependency on libresolv.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "relaydx.h"

static int read_search_domain(char *domain, size_t size)
{
	char line[512];
	char fallback[256] = "";
	FILE *file = fopen("/etc/resolv.conf", "r");

	if (!file)
		return -1;
	while (fgets(line, sizeof(line), file)) {
		char *cursor = line;
		char *value;
		size_t length;

		while (isspace((unsigned char)*cursor))
			cursor++;
		if (*cursor == '#' || *cursor == ';' || *cursor == '\0')
			continue;
		if (!strncmp(cursor, "search", 6) &&
				isspace((unsigned char)cursor[6])) {
			value = cursor + 6;
		} else if (!strncmp(cursor, "domain", 6) &&
				isspace((unsigned char)cursor[6])) {
			value = cursor + 6;
		} else {
			continue;
		}

		while (isspace((unsigned char)*value))
			value++;
		length = strcspn(value, " \t\r\n#;");
		if (length == 0 || length >= sizeof(fallback))
			continue;
		if (!strncmp(cursor, "search", 6)) {
			if (length >= size) {
				fclose(file);
				errno = ENOSPC;
				return -1;
			}
			memcpy(domain, value, length);
			domain[length] = '\0';
			fclose(file);
			return 1;
		}
		memcpy(fallback, value, length);
		fallback[length] = '\0';
	}
	fclose(file);

	if (!fallback[0])
		return 0;
	if (strlen(fallback) >= size) {
		errno = ENOSPC;
		return -1;
	}
	strcpy(domain, fallback);
	return 1;
}

ssize_t relayd_dns_encode_name(const char *name, uint8_t *output,
		size_t output_size)
{
	const char *cursor = name;
	size_t used = 0;

	if (!name || !name[0] || !output) {
		errno = EINVAL;
		return -1;
	}
	while (*cursor) {
		const char *dot = strchr(cursor, '.');
		size_t label_length = dot ? (size_t)(dot - cursor) : strlen(cursor);

		if (label_length == 0) {
			if (dot && dot[1] == '\0')
				break;
			errno = EINVAL;
			return -1;
		}
		if (label_length > 63 || used + 1 + label_length + 1 > output_size ||
				used + 1 + label_length + 1 > 255) {
			errno = ENOSPC;
			return -1;
		}
		output[used++] = (uint8_t)label_length;
		memcpy(output + used, cursor, label_length);
		used += label_length;
		if (!dot)
			break;
		cursor = dot + 1;
	}
	if (used >= output_size) {
		errno = ENOSPC;
		return -1;
	}
	output[used++] = 0;
	return (ssize_t)used;
}

ssize_t relayd_dns_encode_search(uint8_t *output, size_t output_size)
{
	char domain[256];
	int result = read_search_domain(domain, sizeof(domain));

	if (result <= 0)
		return result;
	return relayd_dns_encode_name(domain, output, output_size);
}

ssize_t relayd_dns_decode_name(const uint8_t *message, const uint8_t *end,
		const uint8_t *encoded, char *output, size_t output_size)
{
	const uint8_t *cursor = encoded;
	size_t consumed = 0;
	size_t used = 0;
	unsigned int jumps = 0;
	bool jumped = false;

	if (!message || !end || !encoded || !output || output_size == 0 ||
			message >= end || encoded < message || encoded >= end) {
		errno = EINVAL;
		return -1;
	}

	for (;;) {
		uint8_t label_length;

		if (cursor >= end || jumps > 16)
			goto invalid;
		label_length = *cursor++;
		if (!jumped)
			consumed++;
		if (label_length == 0)
			break;
		if ((label_length & 0xc0) == 0xc0) {
			size_t offset;

			if (cursor >= end)
				goto invalid;
			offset = ((size_t)(label_length & 0x3f) << 8) | *cursor++;
			if (!jumped)
				consumed++;
			if (offset >= (size_t)(end - message))
				goto invalid;
			cursor = message + offset;
			jumped = true;
			jumps++;
			continue;
		}
		if (label_length & 0xc0 || label_length > 63 ||
				(size_t)(end - cursor) < label_length)
			goto invalid;
		if (used && used + 1 >= output_size)
			goto no_space;
		if (used)
			output[used++] = '.';
		if (used + label_length >= output_size)
			goto no_space;
		memcpy(output + used, cursor, label_length);
		used += label_length;
		cursor += label_length;
		if (!jumped)
			consumed += label_length;
	}
	output[used] = '\0';
	return (ssize_t)consumed;

no_space:
	errno = ENOSPC;
	return -1;
invalid:
	errno = EINVAL;
	return -1;
}
