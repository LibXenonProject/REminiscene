/* REminiscence - Flashback interpreter
 * Copyright (C) 2005-2011 Gregory Montoir
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdarg>
#include "util.h"


uint16 g_debugMask;

void debug(uint16 cm, const char *msg, ...) {
	char buf[1024];
	if (cm & g_debugMask) {
		va_list va;
		va_start(va, msg);
		vsprintf(buf, msg, va);
		va_end(va);
		printf("%s\n", buf);
		fflush(stdout);
	}
}

void error(const char *msg, ...) {
	char buf[1024];
	va_list va;
	va_start(va, msg);
	vsnprintf(buf, sizeof(buf), msg, va);
	va_end(va);
	fprintf(stderr, "ERROR: %s!\n", buf);
	exit(-1);
}

void warning(const char *msg, ...) {
	char buf[1024];
	va_list va;
	va_start(va, msg);
	vsnprintf(buf, sizeof(buf), msg, va);
	va_end(va);
	fprintf(stderr, "WARNING: %s!\n", buf);
}

