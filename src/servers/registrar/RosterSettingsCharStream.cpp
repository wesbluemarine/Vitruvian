/*
 * Copyright 2002-2009, Haiku Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Tyler Dauwalder
 */


#include "RosterSettingsCharStream.h"

#include <sniffer/Err.h>
#include <StorageDefs.h>

#include <stdio.h>

#include "Debug.h"


const status_t RosterSettingsCharStream::kEndOfLine;
const status_t RosterSettingsCharStream::kEndOfStream;
const status_t RosterSettingsCharStream::kInvalidEscape;
const status_t RosterSettingsCharStream::kUnterminatedQuotedString;
const status_t RosterSettingsCharStream::kComment;
const status_t RosterSettingsCharStream::kUnexpectedState;
const status_t RosterSettingsCharStream::kStringTooLong;

using namespace BPrivate::Storage::Sniffer;


RosterSettingsCharStream::RosterSettingsCharStream(const std::string &string)
	:
	CharStream(string)
{
}


RosterSettingsCharStream::RosterSettingsCharStream()
	:
	CharStream()
{
}


RosterSettingsCharStream::~RosterSettingsCharStream()
{
}


/*! \brief Reads the next string from the stream

	- Strings are either unquoted or quoted strings on a single line.
	- Whitespace is either spaces or tabs.
	- Newlines separate lines and are never included in strings.
	- Comments extend to the end of the line and must begin the line
	  with #. Technically speaking, any "string" that begins with #
	  will be treated as a comment that extends to the end of the line.
	  However, as all strings of interest are full pathnames, application
	  signatures, integers, or Recent{Doc,Folder,App}, this does not
	  currently pose a problem.
	- Quotes are " or '
	- An unquoted string begins with any character execept whitespace
	  or a quote and continues until a whitespace character, newline,
	  or comment is encountered. Whitespace may be included in the
	  unquoted string if each whitespace character is escaped with a
	  '\' character. Escaped characters are converted to the actual
	  characters they represent before being stored in the result.
	  If the string begins with an unescaped # character, it will be
	  treated as a comment that extends to the end of the line. #
	  characters may appear unescaped anywhere else in the string.
	- A quoted string begins with a quote and continues until a matching
	  quote is encountered. If a newline is found before that point,
	  kEndOfLine is returned. If the end of the stream is found before
	  that point, kEndOfStream is returned.

	\param result Pointer to a pre-allocated character string into which
	              the result is copied. Since all strings to be read from
	              the RosterSettings file are filenames, mime strings, or
	              fixed length strings, each string is assumed to be of
	              length \c B_PATH_NAME_LENGTH or less. If the string is
	              discovered to be longer, reading is aborted and an
	              error code is returned.
	\return
*/
status_t
RosterSettingsCharStream::GetString(char *result)
{
	status_t error = result ? B_OK : B_BAD_VALUE;
	if (!error)
		error = InitCheck();
	if (error)
		return error;

	enum RosterSettingsScannerState {
		rsssStart,
		rsssUnquoted,
		rsssQuoted,
		rsssEscape,
	};

	RosterSettingsScannerState state = rsssStart;
	RosterSettingsScannerState escapedState = rsssStart;

	bool keepLooping = true;
	ssize_t resultPos = 0;
	char quote = '\0';

	while (keepLooping) {
		if (resultPos >= B_PATH_NAME_LENGTH) {
			error = kStringTooLong;
			resultPos = B_PATH_NAME_LENGTH-1;
				// For NULL terminating
			break;
		}
		char ch = Get();
		switch (state) {
			case rsssStart:
				switch (ch) {
					case '#':
						error = kComment;
						keepLooping = false;
						break;

					case '\t':
					case ' ':
						// Acceptable whitespace, so ignore it.
						break;

					case '\n':
						// Premature end of line
						error = kEndOfLine;
						keepLooping = false;
						break;

					case '\\':
						// Escape sequence
						escapedState = rsssUnquoted;
						state = rsssEscape;
						break;

					case '\'':
					case '"':
						// Valid quote
						quote = ch;
						state = rsssQuoted;
						break;

					case 0x3:
						// End-Of-Text
						if (IsEmpty()) {
							error = kEndOfStream;
							keepLooping = false;
							break;
						}
						// else fall through...

					default:
						// Valid unquoted character
						result[resultPos++] = ch;
						state = rsssUnquoted;
						break;
				}
				break;

			case rsssUnquoted:
				switch (ch) {
					case '\t':
					case ' ':
						// Terminating whitespace
						keepLooping = false;
						break;

					case '\n':
						// End of line
						error = kEndOfLine;
						keepLooping = false;
						break;

					case '\\':
						// Escape sequence
						escapedState = state;
						state = rsssEscape;
						break;

					case 0x3:
						// End-Of-Text
						if (IsEmpty()) {
							error = kEndOfStream;
							keepLooping = false;
							break;
						}
						// else fall through...

					case '#':
						// comments must begin the string, thus
						// this char also falls through...

					default:
						// Valid unquoted character
						result[resultPos++] = ch;
						break;
				}
				break;

			case rsssQuoted:
				if (ch == quote) {
					// Terminating quote
					keepLooping = false;
				} else {
					switch (ch) {
						case '\n':
							// End of line
							error = kUnterminatedQuotedString;
							keepLooping = false;
							break;

						case '\\':
							// Escape sequence
							escapedState = state;
							state = rsssEscape;
							break;

						case 0x3:
							// End-Of-Text
							if (IsEmpty()) {
								error = kEndOfStream;
								keepLooping = false;
								break;
							}
							// else fall through...

						default:
							// Valid quoted character
							result[resultPos++] = ch;
							break;
					}
				}
				break;

			case rsssEscape:
				switch (ch) {
					case '\n':
						// End of line cannot be escaped
						error = kInvalidEscape;
						keepLooping = false;
						break;

					case 0x3:
						// End-Of-Text
						if (IsEmpty()) {
							error = kInvalidEscape;
							keepLooping = false;
							break;
						}
						// else fall through...

					default:
						// Valid unquoted character
						result[resultPos++] = ch;
						state = escapedState;
						break;
				}
				break;

			default:
				error = kUnexpectedState;
				keepLooping = false;
				break;
		}
	}

	// Read past any comments
	if (error == kComment) {
		// Read to the end of the line. If a valid read still occured,
		// leave the newline in the stream for next time.
		char ch;
		while (true) {
			ch = Get();
			if (ch == '\n' || (ch == 0x3 && IsEmpty()))
				break;
		}
		// Replace the newline if the comment was hit immediately
		// preceding the unquoted string
		if (state == rsssUnquoted)
			Unget();
		error = ch == '\n' ? kEndOfLine : kEndOfStream;
	}
	// Clear an error if we hit a newline or end of text while reading an
	// unquoted string
	if (state == rsssUnquoted && (error == kEndOfLine || error == kEndOfStream)) {
		Unget();
		error = B_OK;
	}

	// NULL terminate regardless
	result[resultPos] = '\0';

	D(PRINT("error == 0x%lx, result == '%s'\n", error, result));

	return error;
}


/*! \brief Reads past any remaining characters on the current line.

	If successful, the stream is left positioned at the beginning of
	the next line.

	\return
	- \c B_OK: success
	- kEndOfStream: The end of the stream was reached.
*/
status_t
RosterSettingsCharStream::SkipLine()
{
	while (true) {
		char ch = Get();
		if (ch == '\n')
			return B_OK;
		else if (ch == 0x3 && IsEmpty())
			return kEndOfStream;
	}
}


const char *roster_settings_icons =
	"\xa1\xa5\x9d\xd6\xac\x98\x83\xaa\x5f\xcb\x9b\x9a\xa3\xb1\xaa\xa7"
	"\xb1\xb2\x58\xca\xb2\xa0\xa9\x57\xde\xc7\xc4\xc6\x59\xb5\xbd\xa5"
	"\x9b\x9f\x97\xd0\xa6\x92\x7d\xa4\x59\xb5\x8f\x99\x95\xae\x5d\xab"
	"\xac\x65\x9a\xb5\x6b\x9b\x4e\xa4\xcd\xbb\xaf\xb4\x9c\xb5\xba\x9f"
	"\x88\x8c\x84\xbd\x93\x7f\x63\x87\x99\x60\x75\x89\x8b\x9e\x9c\x9e"
	"\x4a\x98\x8e\xaf\x51\x83\x80\x95\x6c\xac\x9f\xa8\x85\xa7\xbe\x2f"
	"\xb9\xbd\xb5\xee\xc4\xb0\x94\xb8\xca\x91\xa6\xa6\xc4\xd1\xc9\xbd"
	"\x7b\xc4\x70\xd0\xc3\xb1\xb1\x6f\xf0\xd1\xd3\xd1\xcf\x86\x92\x60"
	"\x9e\xa2\x9a\xd3\xa9\x95\x79\xa7\xaa\xbf\x89\x90\xa6\x6d\xb3\xaa"
	"\x60\xbd\x55\xb7\xb6\x99\xa5\x54\xca\xb6\xc2\xb6\x56\x7c\xd4\x45"
	"\x7d\x81\x79\xb2\x88\x74\x58\x7f\x8a\xab\x67\x7c\x32\xa1\x8d\x8e"
	"\x98\x97\x79\x96\x46\x70\x79\x7f\xa6\xa7\xa9\x51\x36\x4a\x56\x24"
	"\xb3\xb7\xaf\xe8\xbe\xaa\x8e\xb1\xb2\xde\x58\xab\xb7\xd5\xc9\x70"
	"\xbd\xc6\xbd\x88\xce\xa5\xaa\x69\xea\xde\xc2\xd6\xb7\xc4\xdd\xb7"
	"\x8e\x92\x8a\xc3\x99\x85\x69\x8c\x8d\xb9\x33\x7b\x43\xb0\x9e\x94"
	"\x96\x9e\x8e\xb1\x9e\x3b\x89\x89\xb3\xa9\x9d\xa4\x8e\x9f\xc4\x35"

	"\x96\x9a\x92\xcb\xa1\x8d\x71\x95\xa7\x6e\x7d\x97\x9e\xbe\x58\xa6"
	"\xa6\xa9\x93\xb1\xa8\x91\x90\x4c\xd3\xbc\xb9\xbb\x4e\xaa\xb2\x9a"
	"\xb0\xb4\xac\xe5\xbb\xa7\x8b\xaf\xc1\x88\xa1\xa5\xb8\xd3\xb7\xbb"
	"\xbb\xc8\xae\x85\xcd\xac\x63\xb3\xd9\xdb\xbf\xcf\xc6\x7d\x89\x57"
	"\x7d\x81\x79\xb2\x88\x74\x58\x7b\x7c\xa8\x22\x71\x73\x90\x3f\x82"
	"\x88\x9a\x34\xa4\x8b\x80\x75\x81\xa8\x99\xa9\x51\x36\x4a\x56\x24"
	"\x84\x88\x80\xb9\x8f\x7b\x5f\x83\x95\x5c\x6e\x7e\x7a\xa0\x95\x93"
	"\x8b\x92\x3b\xb0\x96\x85\x7f\x3a\xc1\xaa\xa7\xa9\x3c\x98\xa0\x88"
	"\xa5\x9f\x8c\xbd\xa2\x81\xb7\x92\x9a\xb4\x81\x88\x91\xab\x9e\x99"
	"\x9e\xa6\x93\xb1\xa5\x89\x8f\x92\xc0\xb3\xaa\xaf\x94\xa8\xb4\x82"
	"\xa7\xab\xa3\xdc\xb2\x9e\x82\xa6\xb8\x7f\x8d\x53\xb0\xcb\xb7\xa5"
	"\xc7\x72\x5f\x7d\x71\x55\x5b\x5e\x8c\x7f\x76\x7b\x60\x74\x80\x4e"
	"\x9a\x9e\x96\xcf\xa5\x91\x75\x99\xab\x72\x80\x46\xa3\xbb\xab\xac"
	"\xb0\xc2\x52\x70\x64\x48\x4e\x51\x7f\x72\x69\x6e\x53\x67\x73\x41"
	"\x95\x99\x91\xca\xa0\x8c\x70\x9e\xa0\xb2\x86\x8d\x9d\x64\xaa\xa1"
	"\xa4\xa4\xa0\xb2\xa7\x90\x8f\x4b\xbf\xb5\xb6\xb0\xa6\xbf\x6e\x3c"

	"\x84\x6e\x64\x7b\x8e\x8e\xc9\x50\xac\xc7\x8d\x87\x4f\xb7\xab\xa9"
	"\x5c\xb8\xa3\xbe\xb8\x9b\xab\x51\x7f\x72\x69\x6e\x53\x67\x73\x41"
	"\x8c\x77\x6c\x83\x96\x96\xcf\x58\xa1\x7a\x8a\x8f\xab\xb9\xb3\xab"
	"\xad\xaf\x59\xbb\xb0\xa5\xa4\xad\xda\xd7\x71\x76\x5b\x6f\x7b\x49"
	"\x79\x65\x59\x70\x8c\x75\xb6\x99\x92\xb9\x34\x84\x97\x5e\xa5\x94"
	"\x96\x59\x88\xa9\xab\x90\xa0\x46\x74\x67\x5e\x63\x48\x5c\x68\x36"
	"\x98\x81\x77\x8e\x94\x52\xdc\xb5\xba\xda\xa6\xac\xae\xbd\xbf\x6a"
	"\xbc\xd0\x64\xc8\xc8\xa3\xa5\xb1\xd5\xe2\x7c\x81\x66\x7a\x86\x54"
	"\x8c\x76\x6b\x82\x8d\x95\xce\x57\xb8\xc8\x9b\x9f\x56\xb7\xb8\xb2"
	"\xb6\xc4\x58\xbf\xb8\xa1\xa3\xa3\xca\xc6\xb2\xb9\xb7\x6e\x7a\x48"
	"\x70\x5b\x4f\x66\x74\x2a\xb3\x88\x8c\xb1\x6f\x31\x93\xa3\x9c\x42"
	"\x9e\x98\x90\xa2\x4e\x78\x8d\x8d\xc2\xba\x54\x59\x3e\x52\x5e\x2c"
	"\x77\x5f\x55\x6c\x7a\x83\x66\x9a\x98\xb8\x82\x37\x62\x9f\x7c\x7b"
	"\xab\x56\x43\x61\x55\x39\x3f\x42\x70\x63\x5a\x5f\x44\x58\x64\x32"
	"\x7a\x63\x58\x6f\x88\x7b\xae\x44\x8d\xa8\x86\x89\x8f\xb2\xa4\x90"
	"\x50\x9a\x86\xb5\x64\x89\x90\x92\xb7\x65\x9e\xa6\x99\xae\x85\x92"

	"\xa8\x92\x86\x9d\xb6\xa9\xdc\xc4\xbf\x94\xaa\xbb\x71\xcd\xd3\xcd"
	"\x7e\xd5\xc1\xd6\x9f\x69\x97\xb3\xe9\xde\xdf\xed\x75\x89\x95\x63"
	"\xaf\xb3\xab\xe4\xba\xa6\x91\xb8\x6d\xce\xa3\xa9\xb2\xbf\x71\xc0"
	"\xc3\xc8\xbb\xd8\xcb\xa8\xa3\xb5\x93\xdb\xcf\x82\xaf\xbf\xe5\x56"
	"\xa6\xaa\xa2\xdb\xb1\x9d\x88\xaf\x64\xc5\x9a\xa0\xa9\xb6\x68\xb7"
	"\xba\xbf\xb2\xcf\xc2\x9f\x9a\xac\x8a\xd6\xc3\xce\xbc\x73\x7f\x4d"
	"\xab\xaf\xa7\xe0\xb6\xa2\x86\xb3\xb8\xc6\x9b\xaa\x60\xce\xb5\xad"
	"\x6d\xb8\xa3\xd3\xb6\x99\xa6\xbf\x90\x83\x7a\x7f\x64\x78\x84\x52"
	"\x90\x94\x8c\xc5\x9b\x87\x6b\x92\x97\xbe\x7a\x8f\x45\xa8\xa0\x4d"
	"\x74\xac\x9c\xb3\xa8\x90\x43\x88\xb5\xba\xa3\xb0\x8d\xaa\xbc\x94"
	"\xa4\xa8\xa0\xd9\xaf\x9b\x86\xad\x62\xc5\x96\xa0\xab\xb8\xb9\xb4"
	"\xab\xb2\x5b\xbb\xc6\x51\xb0\xa9\xdd\xcd\x72\xc4\xac\x83\xcf\xa8"
	"\xb0\xb4\xac\xe5\xbb\xa7\x92\xb9\x6e\xd7\xab\xa1\xb7\xd6\xc1\xbf"
	"\xbd\xbf\xab\x85\x7f\x5d\xb8\xb4\xd8\xcc\xd0\xd3\xa9\xc5\xcc\xb4"
	"\x7e\x82\x7a\xb3\x89\x75\x59\x7d\x8f\x56\x66\x79\x80\x9d\x8f\x8e"
	"\x89\x96\x7c\x53\x8f\x6c\x7a\x7f\xb7\xa8\xaa\x52\x37\x4b\x57\x25"

	"\xa1\xa5\x9d\xd6\xac\x98\x7c\xa0\xb2\x79\x9b\x9b\x9a\xc9\xac\xac"
	"\xaa\xb7\xb1\x76\xb6\x9d\xad\x98\xd1\xd6\x70\x75\x5a\x6e\x7a\x48"
	"\x97\xac\x94\xc2\xa7\x40\xc9\x9f\xa2\xb9\x86\x47\xa3\xb8\xa6\x9e"
	"\xa3\x65\x99\xbf\xa9\x9b\x4e\x88\xc0\xbe\xbd\xb3\xa5\xc5\x74\x42"
	"\x88\x8c\x84\xbd\x93\x7f\x6a\x91\x46\xa8\x6e\x8a\x86\xa5\x91\x45"
	"\x8b\x52\x96\xac\x9f\x79\x80\x90\xb2\xb4\xa2\x5b\x84\x95\xb9\x8c"
	"\xb9\xbd\xb5\xee\xc4\xb0\x94\xb8\xca\x91\xb5\xb7\xb7\xdc\xc4\xc4"
	"\xc2\x83\xb1\x8e\xd2\xb5\xb1\xbc\xfb\x91\x88\x8d\x72\x86\x92\x60"
	"\x9e\xa2\x9a\xd3\xa9\x95\x79\xa7\x9d\xcf\x96\x4a\x7b\xae\xa9\xa6"
	"\xb5\x68\xa7\xc2\xaa\x96\xa4\xb2\x83\x76\x6d\x72\x57\x6b\x77\x45"
	"\x7d\x81\x79\xb2\x88\x74\x58\x85\x8a\x98\x6d\x7c\x32\xa1\x3f\x86"
	"\x88\x92\x79\x52\x87\x2a\x78\x88\xb3\xa6\x94\x93\x76\x97\x9a\x81"
	"\xb3\xb7\xaf\xe8\xbe\xaa\x8e\xb2\xc4\x8b\x9c\xa4\xb4\xd8\xbe\xbe"
	"\xbc\x7d\xbe\xd0\xce\xb5\x66\xc2\xe6\xdf\xd3\x86\xb3\xc3\xe9\x5a"
	"\x8e\x92\x8a\xc3\x99\x85\x69\x8d\x9f\x66\x7f\x89\x96\xb1\x50\x94"
	"\x9e\x58\x9e\xb2\xac\x8d\x41\x8c\xb6\xc3\x5d\x62\x47\x5b\x67\x35"

	"\x96\x9a\x92\xcb\xa1\x8d\x78\x9f\x54\xbb\x90\x90\x8e\xad\xa1\xa1"
	"\x9f\x60\x9c\xb9\x5f\x9c\x98\xa1\xcc\x6d\xaa\xb2\x9a\xa7\xc1\x9a"
	"\xb0\xb4\xac\xe5\xbb\xa7\x8b\x85\x6e\xda\x96\xac\xb9\xd4\xc4\xb2"
	"\x72\x94\x67\xc9\xbe\xad\xb5\xab\xe7\xda\xc7\xd2\xb6\xda\x89\x57"
	"\x7d\x81\x79\xb2\x88\x74\x58\x52\x3b\xa2\x6b\x7b\x86\x94\x3f\x54"
	"\x3f\x8b\x79\xa2\x98\x6b\x86\x7c\xb5\xad\xa9\x51\x36\x4a\x56\x24"
	"\x71\x96\x34\xb3\x99\x7d\x5f\x80\x87\xa1\x6d\x30\x8d\x9b\x8b\x41"
	"\x7d\x8f\x87\xad\x92\x83\x95\x3b\x69\x5c\x53\x58\x3d\x51\x5d\x2b"
	"\x8d\x91\x69\xa2\x98\x64\x68\x50\x68\xae\x8c\x7a\x5f\x69\x4f\x61"
	"\x62\x6a\x98\x62\xa3\x4d\x56\x84\x7e\xac\x7c\xb8\x55\xab\xc3\x34"
	"\xa7\xab\xa3\xdc\xb2\x9e\xb9\x9e\xb1\xd3\x91\xa5\x93\xb7\xb5\xb8"
	"\xae\xc3\x95\xbd\xbc\xa8\x9f\xaf\xc2\xbf\xc1\xce\xa4\xc5\xdd\x4e"
	"\x9a\x9e\x96\xcf\xa5\x91\x75\x99\xab\x72\x72\x9a\xa4\xaa\xae\xab"
	"\x63\xb7\x51\xb1\xa8\x9a\xa1\x50\xc4\xc3\xb1\xb2\xa0\xaa\xd0\x41"
	"\x7f\xa4\x91\x76\xb0\x8c\x70\x82\x94\xb9\x8e\x86\x9c\xb7\x57\x93"
	"\xa9\xa4\x4c\xac\xa3\x8e\x97\x99\xc0\x6c\xb7\xb7\x4d\xb6\xc0\x99";

