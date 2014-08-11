#!/usr/bin/env python
# encoding=latin1

# This file is using latin1 with some Big5 literals, to prevent parsing files
# with non-standard Big5 data (ex, Big5-UAO or escape sequence between DBCS).

import re
import sys

import big5

STR_AUTHOR1 = "§@ªÌ:"
STR_AUTHOR2 = "µo«H¤H:"

def ANSI_COLOR(*codes):
    return "\x1b[" + ';'.join(map(str, codes)) + 'm'

ANSI_RESET = ANSI_COLOR()

# Comments format: CommentsPrefix ANSI_COLOR(33) [AUTHOR]
#                  ANSI_RESET ANSI_COLOR(33) ":" [CONTENT]
#                  ANSI_RESET [trailings]
# When AlignComment is set, AUTHOR will be padded with extra SPACE.
# Since we don't store that as an attribute, the simple way (for now) is to keep
# it as-is.
CommentsPrefixes = (
	ANSI_COLOR(1,37) + r'±À ',
	ANSI_COLOR(1,31) + r'¼N ',
	# Also known as <OLDRECOMMEND>, shared by P1 and P2.
	ANSI_COLOR(1,31) + r'¡÷ ')

CommentsFormatRe = (
	'(' + '|'.join(map(re.escape, CommentsPrefixes)) + ')' +
	re.escape(ANSI_COLOR(33)) + '([^\x1b]*)' + re.escape(ANSI_RESET) +
	re.escape(ANSI_COLOR(33) + ':') + '([^\x1b]*)' +
	re.escape(ANSI_RESET) + '(.*)')

# format: "¡° " ANSI_COLOR(1;32) "%s" ANSI_COLOR(0;32) ":Âà¿ý¦Ü" %s
def IsCrossPostLog(buf):
    return (buf.startswith("¡° " + ANSI_COLOR(1,32)) and
	    buf.find(ANSI_COLOR(0,32) + ':Âà¿ý¦Ü') > 0)

def ParseComment(buf):
    """Parses a buffer for known comment formats.

    Returns:
	Dict in (kind, author, content, trailing), or None on failure.
    """
    match = re.findall(CommentsFormatRe, buf)
    if len(match) < 1:
	return None
    match = match[0]
    (kind, author, content, trailing) = match
    return dict(zip(('kind', 'author', 'content', 'trailing'),
	            map(big5.decode, (str(CommentsPrefixes.index(kind) + 1),
				      author, content.rstrip(' '),
				      trailing.rstrip('\n')))))

def ParsePost(contents):
    '''Returns a legacy post into two parts.

    Args:
	contents: A list of input string (file.readlines), or raw string.

    Returns:
	(body, comments): body is the main content without header, and
	                  comments is a list to hold parsed comments.
    '''
    comments = []
    lineno = 0

    if (type(contents) is str):
	contents = contents.splitlines(True)

    # Now, try to skip header.
    if len(contents) < 1:
	return ('', comments)
    author = contents[0]
    if author.startswith(STR_AUTHOR1):
	max_lines = 4
    elif author.startswith(STR_AUTHOR2):
	max_lines = 5
    else:
	max_lines = 0

    # Skip until empty line is seen or max lines reached.
    while len(contents) > 0 and max_lines > 0:
	max_lines -= 1
	if contents.pop(0) == '\n':
	    break

    # TODO A possible enhancement is to start from last found site signature,
    # and convert non-comment as re-edit contents.

    # Remove trailing comments.
    while len(contents) > 0:
	if IsCrossPostLog(contents[-1]):
	    contents.pop(-1)
	    continue
	result = ParseComment(contents[-1])
	if result is None:
	    break
	comments.insert(0, result)
	contents.pop(-1)

    # here's the content.
    return (''.join(contents), comments)

def main(argv):
    if len(argv) == 0:
	filename = 'sample'
    else:
	filename = argv[0]
    print ParsePost(open(filename).readlines())

if __name__ == '__main__':
    main(sys.argv[1:])
