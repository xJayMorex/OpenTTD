/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp_game.cpp Basic functions to receive and send TCP packets for game purposes.
 */

#include "../../stdafx.h"

#include "../network.h"
#include "../network_internal.h"
#include "../../debug.h"
#include "../../error.h"
#include "../../strings_func.h"

#include "table/strings.h"

#include "../../safeguards.h"

static std::vector<std::unique_ptr<NetworkGameSocketHandler>> _deferred_deletions;

/**
 * Create a new socket for the game connection.
 * @param s The socket to connect with.
 */
NetworkGameSocketHandler::NetworkGameSocketHandler(SOCKET s) : NetworkTCPSocketHandler(s),
		last_frame(_frame_counter), last_frame_server(_frame_counter), last_packet(std::chrono::steady_clock::now()) {}

/**
 * Functions to help ReceivePacket/SendPacket a bit
 *  A socket can make errors. When that happens this handles what to do.
 * For clients: close connection and drop back to main-menu
 * For servers: close connection and that is it
 * @return the new status
 */
NetworkRecvStatus NetworkGameSocketHandler::CloseConnection(bool)
{
	/* Clients drop back to the main menu */
	if (!_network_server && _networking) {
		ClientNetworkEmergencySave();
		_switch_mode = SM_MENU;
		_networking = false;
		ShowErrorMessage(GetEncodedString(STR_NETWORK_ERROR_LOSTCONNECTION), {}, WL_CRITICAL);

		return this->CloseConnection(NETWORK_RECV_STATUS_CLIENT_QUIT);
	}

	return this->CloseConnection(NETWORK_RECV_STATUS_CONNECTION_LOST);
}


/**
 * Handle the given packet, i.e. pass it to the right parser receive command.
 * @param p the packet to handle
 * @return #NetworkRecvStatus of handling.
 */
NetworkRecvStatus NetworkGameSocketHandler::HandlePacket(Packet &p)
{
	PacketGameType type = (PacketGameType)p.Recv_uint8();

	if (this->HasClientQuit()) {
		Debug(net, 0, "[tcp/game] Received invalid packet from client {}", this->client_id);
		this->CloseConnection();
		return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	}

	this->last_packet = std::chrono::steady_clock::now();

	switch (type) {
		case PACKET_SERVER_FULL:                  return this->Receive_SERVER_FULL(p);
		case PACKET_SERVER_BANNED:                return this->Receive_SERVER_BANNED(p);
		case PACKET_CLIENT_JOIN:                  return this->Receive_CLIENT_JOIN(p);
		case PACKET_SERVER_ERROR:                 return this->Receive_SERVER_ERROR(p);
		case PACKET_CLIENT_GAME_INFO:             return this->Receive_CLIENT_GAME_INFO(p);
		case PACKET_SERVER_GAME_INFO:             return this->Receive_SERVER_GAME_INFO(p);
		case PACKET_SERVER_CLIENT_INFO:           return this->Receive_SERVER_CLIENT_INFO(p);
		case PACKET_CLIENT_IDENTIFY:              return this->Receive_CLIENT_IDENTIFY(p);
		case PACKET_SERVER_AUTH_REQUEST:          return this->Receive_SERVER_AUTH_REQUEST(p);
		case PACKET_CLIENT_AUTH_RESPONSE:         return this->Receive_CLIENT_AUTH_RESPONSE(p);
		case PACKET_SERVER_ENABLE_ENCRYPTION:     return this->Receive_SERVER_ENABLE_ENCRYPTION(p);
		case PACKET_SERVER_WELCOME:               return this->Receive_SERVER_WELCOME(p);
		case PACKET_CLIENT_GETMAP:                return this->Receive_CLIENT_GETMAP(p);
		case PACKET_SERVER_WAIT:                  return this->Receive_SERVER_WAIT(p);
		case PACKET_SERVER_MAP_BEGIN:             return this->Receive_SERVER_MAP_BEGIN(p);
		case PACKET_SERVER_MAP_SIZE:              return this->Receive_SERVER_MAP_SIZE(p);
		case PACKET_SERVER_MAP_DATA:              return this->Receive_SERVER_MAP_DATA(p);
		case PACKET_SERVER_MAP_DONE:              return this->Receive_SERVER_MAP_DONE(p);
		case PACKET_CLIENT_MAP_OK:                return this->Receive_CLIENT_MAP_OK(p);
		case PACKET_SERVER_JOIN:                  return this->Receive_SERVER_JOIN(p);
		case PACKET_SERVER_FRAME:                 return this->Receive_SERVER_FRAME(p);
		case PACKET_SERVER_SYNC:                  return this->Receive_SERVER_SYNC(p);
		case PACKET_CLIENT_ACK:                   return this->Receive_CLIENT_ACK(p);
		case PACKET_CLIENT_COMMAND:               return this->Receive_CLIENT_COMMAND(p);
		case PACKET_SERVER_COMMAND:               return this->Receive_SERVER_COMMAND(p);
		case PACKET_CLIENT_CHAT:                  return this->Receive_CLIENT_CHAT(p);
		case PACKET_SERVER_CHAT:                  return this->Receive_SERVER_CHAT(p);
		case PACKET_SERVER_EXTERNAL_CHAT:         return this->Receive_SERVER_EXTERNAL_CHAT(p);
		case PACKET_CLIENT_SET_NAME:              return this->Receive_CLIENT_SET_NAME(p);
		case PACKET_CLIENT_QUIT:                  return this->Receive_CLIENT_QUIT(p);
		case PACKET_CLIENT_ERROR:                 return this->Receive_CLIENT_ERROR(p);
		case PACKET_SERVER_QUIT:                  return this->Receive_SERVER_QUIT(p);
		case PACKET_SERVER_ERROR_QUIT:            return this->Receive_SERVER_ERROR_QUIT(p);
		case PACKET_SERVER_SHUTDOWN:              return this->Receive_SERVER_SHUTDOWN(p);
		case PACKET_SERVER_NEWGAME:               return this->Receive_SERVER_NEWGAME(p);
		case PACKET_SERVER_RCON:                  return this->Receive_SERVER_RCON(p);
		case PACKET_CLIENT_RCON:                  return this->Receive_CLIENT_RCON(p);
		case PACKET_SERVER_CHECK_NEWGRFS:         return this->Receive_SERVER_CHECK_NEWGRFS(p);
		case PACKET_CLIENT_NEWGRFS_CHECKED:       return this->Receive_CLIENT_NEWGRFS_CHECKED(p);
		case PACKET_SERVER_MOVE:                  return this->Receive_SERVER_MOVE(p);
		case PACKET_CLIENT_MOVE:                  return this->Receive_CLIENT_MOVE(p);
		case PACKET_SERVER_CONFIG_UPDATE:         return this->Receive_SERVER_CONFIG_UPDATE(p);

		default:
			Debug(net, 0, "[tcp/game] Received invalid packet type {} from client {}", type, this->client_id);
			this->CloseConnection();
			return NETWORK_RECV_STATUS_MALFORMED_PACKET;
	}
}

/**
 * Do the actual receiving of packets.
 * As long as HandlePacket returns OKAY packets are handled. Upon
 * failure, or no more packets to process the last result of
 * HandlePacket is returned.
 * @return #NetworkRecvStatus of the last handled packet.
 */
NetworkRecvStatus NetworkGameSocketHandler::ReceivePackets()
{
	std::unique_ptr<Packet> p;
	while ((p = this->ReceivePacket()) != nullptr) {
		NetworkRecvStatus res = HandlePacket(*p);
		if (res != NETWORK_RECV_STATUS_OKAY) return res;
	}

	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * Helper for logging receiving invalid packets.
 * @param type The received packet type.
 * @return The status the network should have, in this case: "malformed packet error".
 */
NetworkRecvStatus NetworkGameSocketHandler::ReceiveInvalidPacket(PacketGameType type)
{
	Debug(net, 0, "[tcp/game] Received illegal packet type {} from client {}", type, this->client_id);
	return NETWORK_RECV_STATUS_MALFORMED_PACKET;
}

NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_FULL(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_FULL); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_BANNED(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_BANNED); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_JOIN(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_JOIN); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_ERROR(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_ERROR); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_GAME_INFO(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_GAME_INFO); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_GAME_INFO(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_GAME_INFO); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_CLIENT_INFO(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_CLIENT_INFO); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_IDENTIFY(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_IDENTIFY); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_AUTH_REQUEST(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_AUTH_REQUEST); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_AUTH_RESPONSE(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_AUTH_RESPONSE); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_ENABLE_ENCRYPTION(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_ENABLE_ENCRYPTION); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_WELCOME(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_WELCOME); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_GETMAP(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_GETMAP); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_WAIT(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_WAIT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_MAP_BEGIN(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_MAP_BEGIN); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_MAP_SIZE(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_MAP_SIZE); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_MAP_DATA(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_MAP_DATA); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_MAP_DONE(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_MAP_DONE); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_MAP_OK(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_MAP_OK); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_JOIN(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_JOIN); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_FRAME(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_FRAME); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_SYNC(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_SYNC); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_ACK(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_ACK); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_COMMAND(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_COMMAND); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_COMMAND(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_COMMAND); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_CHAT(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_CHAT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_CHAT(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_CHAT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_EXTERNAL_CHAT(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_EXTERNAL_CHAT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_SET_NAME(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_SET_NAME); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_QUIT(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_QUIT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_ERROR(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_ERROR); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_QUIT(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_QUIT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_ERROR_QUIT(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_ERROR_QUIT); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_SHUTDOWN(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_SHUTDOWN); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_NEWGAME(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_NEWGAME); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_RCON(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_RCON); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_RCON(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_RCON); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_CHECK_NEWGRFS(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_CHECK_NEWGRFS); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_NEWGRFS_CHECKED(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_NEWGRFS_CHECKED); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_MOVE(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_MOVE); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_CLIENT_MOVE(Packet &) { return this->ReceiveInvalidPacket(PACKET_CLIENT_MOVE); }
NetworkRecvStatus NetworkGameSocketHandler::Receive_SERVER_CONFIG_UPDATE(Packet &) { return this->ReceiveInvalidPacket(PACKET_SERVER_CONFIG_UPDATE); }

void NetworkGameSocketHandler::DeferDeletion()
{
	_deferred_deletions.emplace_back(this);
	this->is_pending_deletion = true;
}

/* static */ void NetworkGameSocketHandler::ProcessDeferredDeletions()
{
	/* Calls deleter on all items */
	_deferred_deletions.clear();
}
