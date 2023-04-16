-- This is a Wireshark dissector that parses the NetSurveillance protocol
-- To use it in your Wireshark, copy it to your User Plugins folder:
-- Menu Help -> About Wireshark -> Folders tab -> Personal Lua plugins
-- On Windows, this is %appdata%\WireShark\plugins







-- Define the protocol object:
local nsProto = Proto("netsurveillance", "NetSurveillance")

--- Define the fields to be parsed from the protocol:
local fHead     = ProtoField.uint8 ("netsurveillance.head",           "Head",           base.DEC)
local fVersion  = ProtoField.uint8 ("netsurveillance.version",        "Version",        base.DEC)
local fSession  = ProtoField.uint32("netsurveillance.session",        "Session",        base.DEC)
local fSequence = ProtoField.uint32("netsurveillance.sequence",       "Sequence",       base.DEC)
local fTotalPkt = ProtoField.uint8 ("netsurveillance.total_packets",  "Total packets",  base.DEC)
local fCurPkt   = ProtoField.uint8 ("netsurveillance.curr_packet",    "Current packet", base.DEC)
local fMsgType  = ProtoField.uint16("netsurveillance.message_type",   "Message Type",   base.DEC)
local fMsgLen   = ProtoField.uint32("netsurveillance.message_length", "Message Length", base.DEC)
local fMsg      = ProtoField.string("netsurveillance.message",        "Message",        base.ASCII)
nsProto.fields = {fHead, fVersion, fTotalPkt, fCurPkt, fSession, fSequence, fMsgType, fMsgLen, fMsg}

-- Map of MsgType -> MsgTypeText for known message types:
local gMsgTypes =
{
	-- Note: The following values are off-by-one from the official docs, but are what was seen on wire on a real device
	[1000] = "LOGIN_REQ",
	[1001] = "LOGIN_RSP",
	[1002] = "LOGOUT_REQ",
	[1003] = "LOGOUT_RSP",
	[1004] = "FORCELOGOUT_REQ",
	[1005] = "FORCELOGOUT_RSP",
	[1006] = "KEEPALIVE_REQ",
	[1007] = "KEEPALIVE_RSP",
	-- (End of off-by-one values)

	[1020] = "SYSINFO_REQ",
	[1021] = "SYSINFO_RSP",
	[1040] = "CONFIG_SET",
	[1041] = "CONFIG_SET_RSP",
	[1042] = "CONFIG_GET",
	[1043] = "CONFIG_GET_RSP",
	[1044] = "DEFAULT_CONFIG_GET",
	[1045] = "DEFAULT_CONFIG_GET_RSP",
	[1046] = "CONFIG_CHANNELTILE_SET",
	[1047] = "CONFIG_CHANNELTILE_SET_RSP",
	[1048] = "CONFIG_CHANNELTILE_GET",
	[1049] = "CONFIG_CHANNELTILE_GET_RSP",
	[1050] = "CONFIG_CHANNELTILE_DOT_SET",
	[1051] = "CONFIG_CHANNELTILE_DOT_SET_RSP",
	[1052] = "SYSTEM_DEBUG_REQ",
	[1053] = "SYSTEM_DEBUG_RSP",
	[1360] = "ABILITY_GET",
	[1361] = "ABILITY_GET_RSP",
	[1400] = "PTZ_REQ",
	[1401] = "PTZ_RSP",

	[1410] = "MONITOR_REQ",
	[1411] = "MONITOR_RSP",
	[1412] = "MONITOR_DATA",
	[1413] = "MONITOR_CLAIM",
	[1414] = "MONITOR_CLAIM_RSP",

	[1442] = "LOGSEARCH_REQ",
	[1443] = "LOGSEARCH_RSP",

	[1452] = "TIMEQUERY_REQ",
	[1453] = "TIMEQUERY_RSP",

	[1500] = "GUARD_REQ",
	[1501] = "GUARD_RSP",
	[1502] = "UNGUARD_REQ",
	[1503] = "UNGUARD_RSP",

	[1560] = "NETSNAP_REQ",
	[1561] = "NETSNAP_RESP",
	[1562] = "SET_IFRAME_REQ",
	[1563] = "SET_IFRAME_RSP",

	[1590] = "SYNC_TIME_REQ",
	[1591] = "SYNC_TIME_RSP",
}




--- Converts the numeric message type to the text representing the message type:
local function msgTypeText(aMsgType)
	return gMsgTypes[aMsgType] or string.format("Unknown[%d]", aMsgType)
end





--- Parses a single NetSurveillance message from the specified packet, starting at the specified byte offset.
-- Returns the number of bytes consumed, if the message was complete
-- Returns a negative number if the message is not complete and needs data from another packet.
local function dissectNetSurveillanceMessage(aBuffer, aPktInfo, aTreeItem, aByteOffset)

	-- Check if there's an entire message available in the buffer:
	local bytesLeft = aBuffer:len() - aByteOffset
	if (bytesLeft ~= aBuffer:reported_length_remaining(aByteOffset)) then
		-- Only partial packets were captured, do not try to reassemble:
		return 0
	end
	if (bytesLeft < 20) then
		-- We don't have even the message header telling us the message length
		-- Tell Wireshark we need unknown more bytes:
		return -DESEGMENT_ONE_MORE_SEGMENT
	end
	local msgLen = aBuffer(aByteOffset + 16, 4):le_uint()
	if (bytesLeft < msgLen + 20) then
		-- We don't have the full message, but we know how many bytes exactly we're missing:
		return bytesLeft - msgLen - 20
	end

	-- Set the protocol name column:
	aPktInfo.cols.protocol = "NetSurveillance"

	-- Add to the info column:
	local session = aBuffer(aByteOffset + 4, 4):le_uint()
	local msgType = aBuffer(aByteOffset + 14, 2):le_uint()
	aPktInfo.cols.info = tostring(aPktInfo.cols.info) .. string.format("; %s (%d)", msgTypeText(msgType), session)

	-- Add the fields to the tree:
	local subtree = aTreeItem:add(nsProto, aBuffer(), string.format("NetSurveillance protocol data(%d)", aByteOffset))
	subtree:add_le(fHead,     aBuffer(aByteOffset + 0, 1))
	subtree:add_le(fVersion,  aBuffer(aByteOffset + 1, 1))
	subtree:add_le(fSession,  aBuffer(aByteOffset + 4, 4))
	subtree:add_le(fSequence, aBuffer(aByteOffset + 8, 4))
	subtree:add_le(fTotalPkt, aBuffer(aByteOffset + 12, 1))
	subtree:add_le(fCurPkt,   aBuffer(aByteOffset + 13, 1))
	subtree:add_le(fMsgType,  aBuffer(aByteOffset + 14, 2))
	if (gMsgTypes[msgType]) then
		subtree:add("Message Type (string):", gMsgTypes[msgType])
	end
	subtree:add_le(fMsgLen,   aBuffer(aByteOffset + 16, 4))
	subtree:add(fMsg,         aBuffer(aByteOffset + 20, msgLen))

	return 20 + msgLen
end





function nsProto.dissector(aBuffer, aPktInfo, aTreeItem)
	-- Loop until we read all messages in this packet:
	local pktLen = aBuffer:len()
	local currOffset = 0
	while (currOffset < pktLen) do
		local res = dissectNetSurveillanceMessage(aBuffer, aPktInfo, aTreeItem, currOffset)
		if (res > 0) then
			-- A whole message was read, advance and loop again:
			currOffset = currOffset + res
		elseif (res == 0) then
			-- An error occured, bail out:
			return 0
		else
			-- An incomplete message was encountered, set things up so that we get called again with the larger packet:
			aPktInfo.desegment_offset = currOffset
			aPktInfo.desegment_len = -res
			return pktLen
		end
	end
end



local tcpPort = DissectorTable.get("tcp.port")
tcpPort:add(34567, nsProto)
