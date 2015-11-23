/*
 * rcpplus.h
 *
 *  Created on: Aug 21, 2012
 *      Author: arash
 *
 *  This file is part of rcpplus
 *
 *  rcpplus is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  rcpplus is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with rcpplus.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RCPPLUS_H_
#define RCPPLUS_H_

#include <netinet/in.h>

struct rcp_coder_tag;

#define MAX_PAYLOAD_LENGTH		3000
#define MAX_PASSWORD_LENGTH		255
#define MAX_IP_LENGTH	20

typedef struct {
	unsigned short tag;					/* Each tag is represented by two octets. It identifies the command which should be processed by the VideoJet. */
	unsigned char data_type;			/* Specifies the data type of the payload section */
	unsigned char version;				/* The current RCP version is 3. Backward compatibility to version 2 or version 0 is NOT provided. */
	unsigned char rw;					/* Specifies whether the command should read or write. The Read/Write field is coded in the lower nibble of byte */
	unsigned char continuation;			/* This bit signals, when set, that this RCP+ packet is not terminated in the payload */
	unsigned char action;				/* Specifies the kind of the packet. */
	unsigned char request_id;			/* This byte is returned by the VideoJet unchanged. It is up to the user to setup a request ID here to assign the replies to multiple pending requests. */
	unsigned short client_id;			/* Each RCP client register results in a Client ID; this ID has to be provided in all following RCP commands. */
	unsigned int session_id;			/* This ID is used for implementations which need to identify a once registered user in other applications or RCP sessions. */
	unsigned short numeric_descriptor;	/* specifies an attribute for components which are installed more than one time inside the VideoJet, e.g. inputs or relays. */
	unsigned short payload_length;		/* The number of data bytes inside the payload section. The length field itself is not counted. */

	char payload[MAX_PAYLOAD_LENGTH];
} rcp_packet;

typedef struct {
	unsigned int session_id;

	struct sockaddr_in stream_addr;
	int stream_socket;

	struct sockaddr_in tcp_stream_addr;
} rcp_session;

typedef struct {
	char address[MAX_IP_LENGTH];
	int user_level;
	char password[MAX_PASSWORD_LENGTH];

	unsigned short client_id;

	struct sockaddr_in ctrl_addr;
	int control_socket;

} rcp_connection;

extern rcp_connection con;

int rcp_connect(const char* ip);
int rcp_disconnect();

int stream_connect_udp(rcp_session* session);
int stream_connect_tcp(rcp_session* session);

int initiate_tcp_stream(rcp_session* session, struct rcp_coder_tag* coder);

typedef struct {
	unsigned char encapsulation_protocol;
	unsigned char substitude_connection;
	unsigned char relative_addressing;
	unsigned int mta_ip;
	unsigned short mta_port;
	unsigned char coder;
	unsigned char line;
	unsigned short coding;
	unsigned short resolution;
} rcp_media_descriptor;

const char* error_str(int error_code);

#endif /* RCPPLUS_H_ */
