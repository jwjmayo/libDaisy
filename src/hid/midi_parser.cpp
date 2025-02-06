#include "midi_parser.h"

using namespace daisy;

bool MidiParser::Parse(uint8_t byte, MidiEvent* event_out)
{
    bool did_parse = false;

    // Handle real-time messages without altering the parser state
    if (byte >= 0xF8 && byte <= 0xFF)
    {
        if (event_out != nullptr)
        {
            event_out->type = SystemRealTime;
            event_out->srt_type = static_cast<SystemRealTimeType>(byte & kSystemRealTimeMask);
            did_parse = true; // Real-time messages are complete events
        }
        return did_parse;
    }

    // Reset parser when a status byte is received (except during SysEx)
    if ((byte & kStatusByteMask) && pstate_ != ParserSysEx)
    {
        pstate_ = ParserEmpty;
    }

    switch (pstate_)
    {
        case ParserEmpty:
            if (byte & kStatusByteMask)
            {
                // New status byte received
                incoming_message_.channel = byte & kChannelMask;
                incoming_message_.type = static_cast<MidiMessageType>((byte & kMessageMask) >> 4);

                if (incoming_message_.type == SystemRealTime)
                {
                    // Handle System Real-Time messages
                    incoming_message_.srt_type = static_cast<SystemRealTimeType>(byte & kSystemRealTimeMask);
                    pstate_ = ParserEmpty;
                    if (event_out != nullptr)
                    {
                        *event_out = incoming_message_;
                    }
                    did_parse = true;
                }
                else if (incoming_message_.type == SystemCommon)
                {
                    // Handle System Common messages
                    incoming_message_.channel = 0;
                    incoming_message_.sc_type = static_cast<SystemCommonType>(byte & 0x07);

                    if (incoming_message_.sc_type == SystemExclusive)
                    {
                        pstate_ = ParserSysEx;
                        incoming_message_.sysex_message_len = 0;
                    }
                    else
                    {
                        pstate_ = ParserEmpty;
                        if (event_out != nullptr)
                        {
                            *event_out = incoming_message_;
                        }
                        did_parse = true;
                    }
                }
                else
                {
                    // Handle Channel Voice messages
                    pstate_ = ParserHasStatus;
                    running_status_ = incoming_message_.type;
                }
            }
            else
            {
                // Handle running status
                if (running_status_ != MessageLast)
                {
                    incoming_message_.type = running_status_;
                    incoming_message_.data[0] = byte & kDataByteMask;

                    if (running_status_ == ChannelPressure || running_status_ == ProgramChange)
                    {
                        pstate_ = ParserEmpty;
                        if (event_out != nullptr)
                        {
                            *event_out = incoming_message_;
                        }
                        did_parse = true;
                    }
                    else
                    {
                        pstate_ = ParserHasData0;
                    }
                }
            }
            break;

        case ParserHasStatus:
            if ((byte & kStatusByteMask) == 0)
            {
                incoming_message_.data[0] = byte & kDataByteMask;

                if (running_status_ == ChannelPressure || running_status_ == ProgramChange)
                {
                    pstate_ = ParserEmpty;
                    if (event_out != nullptr)
                    {
                        *event_out = incoming_message_;
                    }
                    did_parse = true;
                }
                else
                {
                    pstate_ = ParserHasData0;
                }
            }
            else
            {
                // New status byte received, reset parser
                pstate_ = ParserEmpty;
                return Parse(byte, event_out); // Reprocess the byte as a new status
            }
            break;

        case ParserHasData0:
            if ((byte & kStatusByteMask) == 0)
            {
                incoming_message_.data[1] = byte & kDataByteMask;

                // Handle NoteOn with velocity 0 as NoteOff
                if (running_status_ == NoteOn && incoming_message_.data[1] == 0)
                {
                    incoming_message_.type = NoteOff;
                }

                if (event_out != nullptr)
                {
                    *event_out = incoming_message_;
                }
                did_parse = true;
            }
            pstate_ = ParserEmpty;
            break;

        case ParserSysEx:
            if (byte == 0xF7)
            {
                // End of SysEx message
                pstate_ = ParserEmpty;
                if (event_out != nullptr)
                {
                    *event_out = incoming_message_;
                }
                did_parse = true;
            }
            else if (incoming_message_.sysex_message_len < SYSEX_BUFFER_LEN)
            {
                // Store SysEx data
                incoming_message_.sysex_data[incoming_message_.sysex_message_len++] = byte;
            }
            else
            {
                // SysEx buffer overflow, discard excess data
                pstate_ = ParserEmpty;
            }
            break;

        default:
            break;
    }

    return did_parse;
}

void MidiParser::Reset()
{
    pstate_ = ParserEmpty;
    incoming_message_.type = MessageLast;
    running_status_ = MessageLast;
}