// Copyright (C) 2007, Fredrik Kuivinen <frekui@gmail.com>
//               2007, Petr Baudis <pasky@suse.cz>
//          2008-2009, Jakub Narebski <jnareb@gmail.com>

/* ============================================================ */
/* generic utility functions */

var DEBUG = 0;
function debug(str) {
	if (DEBUG) {
		alert(str);
	}
}

// convert month or day of the month to string, padding it with
// '0' (zero) to two characters width if necessary, e.g. 2 -> '02'
function zeroPad(n) {
	if (n < 10) {
		return '0' + n;
	} else {
		return n.toString();
	}
}

// pad number N with nonbreakable spaces on the left, to WIDTH characters
// example: spacePad(12, 3) == '&nbsp;12' ('&nbsp;' is nonbreakable space)
function spacePad(n, width) {
	var prefix = '';

	width -= n.toString().length;
	while (width > 1) {
		prefix += '&nbsp;';
		width--;
	}
	return prefix + n;
}

/**
 * @param {string} input: input value converted to string.
 * @param {number} size: desired length of output.
 * @param {string} ch: single character to prefix to string.
 */
function padLeft(input, size, ch) {
	var s = input + "";
	while (s.length < size) {
		s = ch + s;
	}
	return s;
}

// create XMLHttpRequest object in cross-browser way
function createRequestObject() {
	try {
		return new XMLHttpRequest();
	} catch (e) {}
	try {
		return new ActiveXObject("Msxml2.XMLHTTP");
	} catch (e) {}
	try {
		return new ActiveXObject("Microsoft.XMLHTTP");
	} catch (e) {}

	debug("XMLHttpRequest not supported");
	return null;
}

/* ============================================================ */
/* utility/helper functions (and variables) */

var http;       // XMLHttpRequest object
var projectUrl; // partial query

// 'commits' is an associative map. It maps SHA1s to Commit objects.
var commits = {};

// constructor for Commit objects, used in 'blame'
function Commit(sha1) {
	this.sha1 = sha1;
	this.nprevious = 0; /* blame-specific */
}

/* ............................................................ */
/* progress info, timing, error reporting */

var blamedLines = 0;
var totalLines  = '???';
var div_progress_bar;
var div_progress_info;

// how many lines does a file have, used in progress info
function countLines() {
	var table =
		document.getElementById('blame_table') ||
		document.getElementsByTagName('table')[0];

	if (table) {
		return table.getElementsByTagName('tr').length - 1; // for header
	} else {
		return '...';
	}
}

// update progress info and length (width) of progress bar
function updateProgressInfo() {
	if (!div_progress_info) {
		div_progress_info = document.getElementById('progress_info');
	}
	if (!div_progress_bar) {
		div_progress_bar = document.getElementById('progress_bar');
	}
	if (!div_progress_info && !div_progress_bar) {
		return;
	}

	var percentage = Math.floor(100.0*blamedLines/totalLines);

	if (div_progress_info) {
		//div_progress_info.firstChild.data = ... /* text node */ ???
		div_progress_info.innerHTML  = blamedLines + ' / ' + totalLines +
			' (' + spacePad(percentage, 3) + '%)';
	}

	if (div_progress_bar) {
		//div_progress_bar.setAttribute('style', 'width: '+percentage+'%;');
		div_progress_bar.style.width = percentage + '%';
	}
}


var t_interval_server = '';
var cmds_server = '';
var t0 = new Date();

// write how much it took to generate data, and to run script
function writeTimeInterval() {
	var info_time = document.getElementById('generate_time');
	if (!info_time || !t_interval_server) {
		return;
	}
	var t1 = new Date();
	//info_time.firstChild.data = ... /* text node */ ???
	info_time.innerHTML += ' + (' +
		t_interval_server + 's server blame_data / ' +
		(t1.getTime() - t0.getTime())/1000 + 's client JavaScript)';

	var info_cmds = document.getElementById('generate_cmd');
	if (!info_time || !cmds_server) {
		return;
	}
	info_cmds.innerHTML += ' + ' + cmds_server;
}

// show an error message alert to user within page
function errorInfo(str) {
	if (!div_progress_info) {
		div_progress_info = document.getElementById('progress_info');
	}
	if (div_progress_info) {
		div_progress_info.className = 'error';
		div_progress_info.innerHTML = str; /* can contain HTML */
	}
}

/* ............................................................ */
/* coloring rows during blame_data (git blame --incremental) run */

// used to extract N from colorN, where N is a number,
var colorRe = /\bcolor([0-9]*)\b/;

// return N if <tr class="colorN">, otherwise return null
// (some browsers require CSS class names to begin with letter)
function getColorNo(tr) {
	if (!tr) {
		return null;
	}
	var className = tr.className;
	if (className) {
		var match = colorRe.exec(className);
		if (match) {
			return parseInt(match[1], 10);
		}
	}
	return null;
}

// return one of given possible colors (curently least used one)
// example: chooseColorNoFrom(2, 3) returns 2 or 3
var colorsFreq = [0, 0, 0];
// assumes that  1 <= arguments[i] <= colorsFreq.length
function chooseColorNoFrom() {
	// choose the color which is least used
	var colorNo = arguments[0];
	for (var i = 1; i < arguments.length; i++) {
		if (colorsFreq[arguments[i]-1] < colorsFreq[colorNo-1]) {
			colorNo = arguments[i];
		}
	}
	colorsFreq[colorNo-1]++;
	return colorNo;
}

// given two neigbour <tr> elements, find color which would be different
// from color of both of neighbours; used to 3-color blame table
function findColorNo(tr_prev, tr_next) {
	var color_prev = getColorNo(tr_prev);
	var color_next = getColorNo(tr_next);


	// neither of neighbours has color set
	// THEN we can use any of 3 possible colors
	if (!color_prev && !color_next) {
		return chooseColorNoFrom(1,2,3);
	}

	// either both neighbours have the same color,
	// or only one of neighbours have color set
	// THEN we can use any color except given
	var color;
	if (color_prev === color_next) {
		color = color_prev; // = color_next;
	} else if (!color_prev) {
		color = color_next;
	} else if (!color_next) {
		color = color_prev;
	}
	if (color) {
		return chooseColorNoFrom((color % 3) + 1, ((color+1) % 3) + 1);
	}

	// neighbours have different colors
	// THEN there is only one color left
	return (3 - ((color_prev + color_next) % 3));
}

/* ............................................................ */
/* coloring rows like 'blame' after 'blame_data' finishes */

// returns true if given row element (tr) is first in commit group
function isStartOfGroup(tr) {
	return tr.firstChild.className === 'sha1';
}

// change colors to use zebra coloring (2 colors) instead of 3 colors
// concatenate neighbour commit groups belonging to the same commit
function fixColorsAndGroups() {
	var colorClasses = ['light', 'dark'];
	var linenum = 1;
	var tr, prev_group;
	var colorClass = 0;
	var table =
		document.getElementById('blame_table') ||
		document.getElementsByTagName('table')[0];

	while ((tr = document.getElementById('l'+linenum))) {
	// index origin is 0, which is table header
	//while ((tr = table.rows[linenum])) {
		if (isStartOfGroup(tr, linenum, document)) {
			if (prev_group &&
			    prev_group.firstChild.firstChild.href ===
			            tr.firstChild.firstChild.href) {
				// we have to concatenate groups
				var prev_rows = prev_group.firstChild.rowSpan || 1;
				var curr_rows =         tr.firstChild.rowSpan || 1;
				prev_group.firstChild.rowSpan = prev_rows + curr_rows;
				//tr.removeChild(tr.firstChild);
				tr.deleteCell(0); // DOM2 HTML way
			} else {
				colorClass = (colorClass + 1) % 2;
				prev_group = tr;
			}
		}
		var tr_class = tr.className;
		tr.className = tr_class.replace(colorRe, colorClasses[colorClass]);
		linenum++;
	}
}

/* ............................................................ */
/* time and data */

// used to extract hours and minutes from timezone info, e.g '-0900'
var tzRe = /^([+-][0-9][0-9])([0-9][0-9])$/;

// return date in local time formatted in iso-8601 like format
// 'yyyy-mm-dd HH:MM:SS +/-ZZZZ' e.g. '2005-08-07 21:49:46 +0200'
function formatDateISOLocal(epoch, timezoneInfo) {
	var match = tzRe.exec(timezoneInfo);
	// date corrected by timezone
	var localDate = new Date(1000 * (epoch +
		(parseInt(match[1],10)*3600 + parseInt(match[2],10)*60)));
	var localDateStr = // e.g. '2005-08-07'
		localDate.getUTCFullYear()         + '-' +
		zeroPad(localDate.getUTCMonth()+1) + '-' +
		zeroPad(localDate.getUTCDate());
	var localTimeStr = // e.g. '21:49:46'
		zeroPad(localDate.getUTCHours())   + ':' +
		zeroPad(localDate.getUTCMinutes()) + ':' +
		zeroPad(localDate.getUTCSeconds());

	return localDateStr + ' ' + localTimeStr + ' ' + timezoneInfo;
}

/* ............................................................ */
/* unquoting/unescaping filenames */

var escCodeRe = /\\([^0-7]|[0-7]{1,3})/g;

// unquote maybe git-quoted filename
function unquote(str) {
	function unq(seq) {
		var es = { // character escape codes, aka escape sequences
			t: "\t",   // tab            (HT, TAB)
			n: "\n",   // newline        (NL)
			r: "\r",   // return         (CR)
			f: "\f",   // form feed      (FF)
			b: "\b",   // backspace      (BS)
			a: "\x07", // alarm (bell)   (BEL)
			e: "\x1B", // escape         (ESC)
			v: "\v"    // vertical tab   (VT)
		};

		if (seq.search(/^[0-7]{1,3}$/) !== -1) {
			// octal char sequence
			return String.fromCharCode(parseInt(seq, 8));
		} else if (seq in es) {
			// C escape sequence, aka character escape code
			return es[seq];
		}
		// quoted ordinary character
		return seq;
	}

	var match = str.match(/^\"(.*)\"$/);
	if (match) {
		str = match[1];
		// perhaps str = eval('"'+str+'"'); would be enough?
		str = str.replace(escCodeRe,
			function (substr, p1, offset, s) { return unq(p1); });
	}
	return str;
}

/* ============================================================ */
/* main part: parsing response */

// called for each blame entry, as soon as it finishes
function handleLine(commit) {
	/*
	   This is the structure of the HTML fragment we are working
	   with:

	   <tr id="l123" class="">
	     <td class="sha1" title=""><a href=""></a></td>
	     <td class="linenr"><a class="linenr" href="">123</a></td>
	     <td class="pre"># times (my ext3 doesn&#39;t).</td>
	   </tr>
	*/

	var resline = commit.resline;

	// format date and time string only once per commit
	if (!commit.info) {
		/* e.g. 'Kay Sievers, 2005-08-07 21:49:46 +0200' */
		commit.info = commit.author + ', ' +
			formatDateISOLocal(commit.authorTime, commit.authorTimezone);
	}

	// color depends on group of lines, not only on blamed commit
	var colorNo = findColorNo(
		document.getElementById('l'+(resline-1)),
		document.getElementById('l'+(resline+commit.numlines))
	);

	// loop over lines in commit group
	for (var i = 0; i < commit.numlines; i++) {
		var tr = document.getElementById('l'+resline);
		if (!tr) {
			debug('tr is null! resline: ' + resline);
			break;
		}
		/*
			<tr id="l123" class="">
			  <td class="sha1" title=""><a href=""></a></td>
			  <td class="linenr"><a class="linenr" href="">123</a></td>
			  <td class="pre"># times (my ext3 doesn&#39;t).</td>
			</tr>
		*/
		var td_sha1  = tr.firstChild;
		var a_sha1   = td_sha1.firstChild;
		var a_linenr = td_sha1.nextSibling.firstChild;

		/* <tr id="l123" class=""> */
		var tr_class = '';
		if (colorNo !== null) {
			tr_class = 'color'+colorNo;
		}
		if (commit.boundary) {
			tr_class += ' boundary';
		}
		if (commit.nprevious === 0) {
			tr_class += ' no-previous';
		} else if (commit.nprevious > 1) {
			tr_class += ' multiple-previous';
		}
		tr.className = tr_class;

		/* <td class="sha1" title="?" rowspan="?"><a href="?">?</a></td> */
		if (i === 0) {
			td_sha1.title = commit.info;
			td_sha1.rowSpan = commit.numlines;

			a_sha1.href = projectUrl + ';a=commit;h=' + commit.sha1;
			//a_sha1.firstChild.data = ... /* text node */ ???
			a_sha1.innerHTML = commit.sha1.substr(0, 8);
			if (commit.numlines >= 2) {
				var br   = document.createElement("br");
				var text = document.createTextNode(
					commit.author.match(/\b([A-Z])\B/g).join(''));
				if (br && text) {
					td_sha1.appendChild(br);
					td_sha1.appendChild(text);
				}
			}
		} else {
			//tr.removeChild(td_sha1); // DOM2 Core way
			tr.deleteCell(0); // DOM2 HTML way
		}

		/* <td class="linenr"><a class="linenr" href="?">123</a></td> */
		var linenr_commit =
			('previous' in commit ? commit.previous : commit.sha1);
		var linenr_filename =
			('file_parent' in commit ? commit.file_parent : commit.filename);
		a_linenr.href = projectUrl + ';a=blame_incremental' +
			';hb=' + linenr_commit +
			';f='  + encodeURIComponent(linenr_filename) +
			'#l' + (commit.srcline + i);

		resline++;
		blamedLines++;

		//updateProgressInfo();
	}
}

// ----------------------------------------------------------------------

var prevDataLength = -1;
var nextLine = 0;
var inProgress = false;

var sha1Re = /^([0-9a-f]{40}) ([0-9]+) ([0-9]+) ([0-9]+)/;
var infoRe = /^([a-z-]+) ?(.*)/;
var endRe  = /^END ?([^ ]*) ?(.*)/;
var curCommit = new Commit();

var pollTimer = null;

// handler for XMLHttpRequest onreadystatechange events
function handleResponse() {
	debug('handleResp ready: ' + http.readyState +
	      ' respText null?: ' + (http.responseText === null) +
	      ' progress: ' + inProgress);

	if (http.readyState !== 4 && http.readyState !== 3) {
		return;
	}

	// the server returned error
	if (http.readyState === 3 && http.status !== 200) {
		return;
	}
	if (http.readyState === 4 && http.status !== 200) {
		if (!div_progress_info) {
			div_progress_info = document.getElementById('progress_info');
		}

		errorInfo('Server error: ' +
			http.status + ' - ' + (http.statusText || 'Error contacting server'));

		clearInterval(pollTimer);
		inProgress = false;
	}

	// In konqueror http.responseText is sometimes null here...
	if (http.responseText === null) {
		return;
	}

	// in case we were called before finished processing
	if (inProgress) {
		return;
	} else {
		inProgress = true;
	}

	while (prevDataLength !== http.responseText.length) {
		if (http.readyState === 4 &&
		    prevDataLength === http.responseText.length) {
			break;
		}

		prevDataLength = http.responseText.length;
		var response = http.responseText.substring(nextLine);
		var lines = response.split('\n');
		nextLine = nextLine + response.lastIndexOf('\n') + 1;
		if (response[response.length-1] !== '\n') {
			lines.pop();
		}

		for (var i = 0; i < lines.length; i++) {
			var match = sha1Re.exec(lines[i]);
			if (match) {
				var sha1 = match[1];
				var srcline  = parseInt(match[2], 10);
				var resline  = parseInt(match[3], 10);
				var numlines = parseInt(match[4], 10);
				var c = commits[sha1];
				if (!c) {
					c = new Commit(sha1);
					commits[sha1] = c;
				}

				c.srcline = srcline;
				c.resline = resline;
				c.numlines = numlines;
				curCommit = c;

			} else if ((match = infoRe.exec(lines[i]))) {
				var info = match[1];
				var data = match[2];
				switch (info) {
				case 'filename':
					curCommit.filename = unquote(data);
					// 'filename' information terminates the entry
					handleLine(curCommit);
					updateProgressInfo();
					break;
				case 'author':
					curCommit.author = data;
					break;
				case 'author-time':
					curCommit.authorTime = parseInt(data, 10);
					break;
				case 'author-tz':
					curCommit.authorTimezone = data;
					break;
				case 'previous':
					curCommit.nprevious++;
					if (!'previous' in curCommit) {
						var parts = data.split(' ', 2);
						curCommit.previous    = parts[0];
						curCommit.file_parent = unquote(parts[1]);
					}
					break;
				case 'boundary':
					debug('Boundary commit: '+curCommit.sha1);
					curCommit.boundary = true;
					break;
				} // end switch

			} else if ((match = endRe.exec(lines[i]))) {
				t_interval_server = match[1];
				cmds_server = match[2];
				debug('END: '+lines[i]);
			} else if (lines[i] !== '') {
				debug('malformed line: ' + lines[i]);
			}
		}
	}

	// did we finish work?
	if (http.readyState === 4 &&
	    prevDataLength === http.responseText.length) {
		clearInterval(pollTimer);

		fixColorsAndGroups();
		writeTimeInterval();
		commits = {}; // free memory
	}

	inProgress = false;
}

// ============================================================
// ------------------------------------------------------------

/*
	Function: startBlame

	Incrementally update line data in blame_incremental view in gitweb.

	Parameters:

		blamedataUrl - URL to server script generating blame data.
		bUrl -partial URL to project, used to generate links in blame.

	Comments:

	Called from 'blame_incremental' view after loading table with
	file contents, a base for blame view.
*/
function startBlame(blamedataUrl, bUrl) {
	debug('startBlame('+blamedataUrl+', '+bUrl+')');

	http = createRequestObject();
	if (!http) {
		errorInfo('<b>ERROR:</b> XMLHttpRequest not supported');
		return;
	}

	t0 = new Date();
	projectUrl = bUrl;
	if ((div_progress_bar = document.getElementById('progress_bar'))) {
		div_progress_bar.setAttribute('style', 'width: 100%;');
		//div_progress_bar.style.value = 'width: 100%;'; // doesn't work
	}
	totalLines = countLines();
	updateProgressInfo();

	http.open('get', blamedataUrl);
	http.setRequestHeader('Accept', 'text/plain'); // in case of future changes
	// perhaps also, in the future, 'multipart/x-mixed-replace' (not standard)
	http.onreadystatechange = handleResponse;
	//http.onreadystatechange = function () { handleResponse(http); };
	http.send(null);

	// not all browsers call onreadystatechange event on each server flush
	if (!DEBUG) {
		pollTimer = setInterval(handleResponse, 1000);
	}
}

// end of blame.js
