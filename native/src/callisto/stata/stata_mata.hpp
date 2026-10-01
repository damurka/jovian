#ifndef CALLISTO_STATA_MATA_HPP
#define CALLISTO_STATA_MATA_HPP

// Callisto's Mata library: what the kernel reads from Stata -- names, the dataset's description, its values -- as
// pystata reads them through sfi (which is Python's only, not C's). Each function writes its answer as JSON to the
// file it is given (StataInterpreter::mataJson()), so nothing is printed and nothing is wrapped at c(linesize).
//
// At start-up the source is written to a do-file (@DIR@ replaced by a folder of the kernel's own) and run: it compiles
// the functions into lcallisto.mlib there and puts the folder on the adopath, so Mata finds them again after a
// `clear all` or `mata clear` (the library is loaded when one of its functions is called). The functions are named
// callisto_*, apart from the user's.
//
// The do-file is read by Stata's do-file processor: no $ (a global macro) and no backtick (a local macro) in it.

namespace callisto { namespace stata {

    inline constexpr const char* kMataLibrary = R"MATA(version 17
mata:
mata set matastrict off

// A JSON string
string scalar callisto_jstr(string scalar s)
{
    real scalar i
    s = subinstr(s, "\", "\\", .)
    s = subinstr(s, char(34), "\" + char(34), .)
    s = subinstr(s, char(10), "\n", .)
    s = subinstr(s, char(13), "\r", .)
    s = subinstr(s, char(9), "\t", .)
    if (ustrregexm(s, "[\x01-\x1f]")) {
        for (i = 1; i <= 31; i++) {
            if (strpos(s, char(i))) s = subinstr(s, char(i), "\u00" + (i < 16 ? "0" : "") + inbase(16, i), .)
        }
    }
    return(char(34) + s + char(34))
}

// A JSON number: null for the system missing value, a string for an extended one (".a")
string scalar callisto_jnum(real scalar x)
{
    string scalar s
    if (x == .) return("null")
    if (missing(x)) return(char(34) + strofreal(x) + char(34))
    s = strtrim(strofreal(x, "%21.0g"))
    if (substr(s, 1, 1) == ".") s = "0" + s
    else if (substr(s, 1, 2) == "-.") s = "-0" + substr(s, 2, .)
    return(s)
}

string scalar callisto_jstrs(string vector v)
{
    real scalar i
    string scalar out
    out = ""
    for (i = 1; i <= length(v); i++) out = out + (i > 1 ? "," : "") + callisto_jstr(v[i])
    return("[" + out + "]")
}

void callisto_write(string scalar path, string scalar text)
{
    real scalar fh
    if (fileexists(path)) unlink(path)
    fh = fopen(path, "w")
    fwrite(fh, text)
    fclose(fh)
}

// Names: variables, globals, locals (of the interactive level: called from the prompt), scalars, stored results
// (r(), e(), s()), the graphs _gr_list recorded, the adopath's folders
string colvector callisto_list(string scalar kind)
{
    if (kind == "variables") return(st_nvar() ? vec(st_varname(1..st_nvar())) : J(0, 1, ""))
    if (kind == "globals") return(st_dir("global", "macro", "*"))
    if (kind == "locals") return(st_dir("local", "macro", "*"))
    if (kind == "scalars") return(st_dir("global", "numscalar", "*") \ st_dir("global", "strscalar", "*"))
    if (kind == "r()" | kind == "e()" | kind == "s()") {
        return(st_dir(kind, "macro", "*") \ st_dir(kind, "numscalar", "*") \ st_dir(kind, "matrix", "*"))
    }
    if (kind == "graphs") return(vec(tokens(st_global("r(_grlist)"))))
    if (kind == "adopath") return(vec(pathsubsysdir(pathlist(c("adopath")))))
    return(J(0, 1, ""))
}

void callisto_names(string scalar kind, string scalar path)
{
    callisto_write(path, callisto_jstrs(callisto_list(kind)))
}

// The dataset in memory: its frame, size, file, variables (name, type, format, label, value label) and value labels
// (at most `maxlabels` values each)
void callisto_dataset(string scalar path, real scalar maxlabels)
{
    real scalar i, k, n
    real colvector values
    string colvector text, labelnames
    string scalar out, vl, q

    q = char(34)
    k = st_nvar()
    out = "{" + q + "frame" + q + ":" + callisto_jstr(st_framecurrent())
    out = out + "," + q + "observations" + q + ":" + callisto_jnum(st_nobs())
    out = out + "," + q + "filename" + q + ":" + callisto_jstr(c("filename"))
    out = out + "," + q + "changed" + q + ":" + (c("changed") ? "true" : "false")
    out = out + "," + q + "variables" + q + ":["
    labelnames = J(0, 1, "")
    for (i = 1; i <= k; i++) {
        vl = st_varvaluelabel(i)
        out = out + (i > 1 ? "," : "") + "{" + q + "name" + q + ":" + callisto_jstr(st_varname(i))
        out = out + "," + q + "type" + q + ":" + callisto_jstr(st_vartype(i))
        out = out + "," + q + "format" + q + ":" + callisto_jstr(st_varformat(i))
        out = out + "," + q + "label" + q + ":" + callisto_jstr(st_varlabel(i))
        out = out + "," + q + "valueLabel" + q + ":" + (vl == "" ? "null" : callisto_jstr(vl)) + "}"
        if (vl != "") {
            if (st_vlexists(vl) & !anyof(labelnames, vl)) labelnames = labelnames \ vl
        }
    }
    out = out + "]," + q + "valueLabels" + q + ":{"
    for (i = 1; i <= rows(labelnames); i++) {
        st_vlload(labelnames[i], values, text)
        n = min((rows(values), maxlabels))
        out = out + (i > 1 ? "," : "") + callisto_jstr(labelnames[i]) + ":{" + q + "values" + q + ":["
        if (n > 0) out = out + invtokens(strtrim(strofreal(values[1..n], "%21.0g"))', ",")
        out = out + "]," + q + "labels" + q + ":" + (n > 0 ? callisto_jstrs(text[1..n]) : "[]") + "}"
    }
    callisto_write(path, out + "}}")
}

// JSON strings of a column of strings (callisto_jstr() for each, at once)
string colvector callisto_jstrv(string colvector s)
{
    real scalar i
    s = subinstr(s, "\", "\\", .)
    s = subinstr(s, char(34), "\" + char(34), .)
    s = subinstr(s, char(10), "\n", .)
    s = subinstr(s, char(13), "\r", .)
    s = subinstr(s, char(9), "\t", .)
    if (any(ustrregexm(s, "[\x01-\x1f]"))) {
        for (i = 1; i <= 31; i++) s = subinstr(s, char(i), "\u00" + (i < 16 ? "0" : "") + inbase(16, i), .)
    }
    return(char(34) :+ s :+ char(34))
}

// JSON numbers of a column of numbers (callisto_jnum() for each, at once)
string colvector callisto_jnumv(real colvector x)
{
    string colvector s
    real colvector m
    real scalar i
    s = strtrim(strofreal(x, "%21.0g"))
    s = ustrregexra(s, "^\.(?=[0-9])", "0.")
    s = ustrregexra(s, "^-\.(?=[0-9])", "-0.")
    // missing() of a vector counts them; every missing value is >= .
    m = selectindex(x :>= .)
    for (i = 1; i <= rows(m); i++) s[m[i]] = (x[m[i]] == . ? "null" : char(34) + s[m[i]] + char(34))
    return(s)
}

// Observations `from` to `to` of the variables `vars` (names, separated by spaces), as rows: values (numbers, null
// for the system missing value, ".a" for an extended one, strings) or, `formatted`, as the Data Editor shows them
// (value labels, display formats). A column at a time.
void callisto_rows(string scalar path, string scalar vars, real scalar from, real scalar to, real scalar formatted)
{
    real rowvector idx
    real colvector x, m
    real scalar j
    string colvector rows, cell, labeled
    string scalar vl

    idx = st_varindex(tokens(vars))
    rows = J(to - from + 1, 1, "")
    for (j = 1; j <= cols(idx); j++) {
        if (st_isstrvar(idx[j])) {
            cell = callisto_jstrv(st_sdata((from, to), idx[j]))
        }
        else {
            x = st_data((from, to), idx[j])
            if (formatted) {
                cell = strtrim(strofreal(x, st_varformat(idx[j])))
                vl = st_varvaluelabel(idx[j])
                if (vl != "") {
                    labeled = st_vlmap(vl, x)
                    m = selectindex(labeled :!= "")
                    if (rows(m)) cell[m] = labeled[m]
                }
                cell = callisto_jstrv(cell)
            }
            else cell = callisto_jnumv(x)
        }
        rows = rows :+ (j > 1 ? "," : "") :+ cell
    }
    callisto_write(path, "[" + invtokens(("[" :+ rows :+ "]")', ",") + "]")
}

// What browse (browse.ado, written beside the library) asked for since the last look: {"requested", "variables"}
void callisto_take_view(string scalar path)
{
    string scalar q
    q = char(34)
    if (st_global("CALLISTO_VIEW_ON") != "1") {
        callisto_write(path, "{" + q + "requested" + q + ":false}")
        return
    }
    callisto_write(path, "{" + q + "requested" + q + ":true," + q + "variables" + q + ":" + callisto_jstrs(tokens(st_global("CALLISTO_VIEW"))) + "}")
    st_global("CALLISTO_VIEW_ON", "")
    st_global("CALLISTO_VIEW", "")
}

mata mlib create lcallisto, dir("@DIR@") replace
mata mlib add lcallisto callisto_*(), dir("@DIR@")
mata drop callisto_*()
end
adopath + "@DIR@"
mata: mata mlib index
)MATA";

    // browse, and edit and the abbreviations: there is no Data Editor in an embedded Stata, so they ask the host to
    // open its data viewer (StataInterpreter::askToViewData(), after the cell). Written beside the library, on the
    // adopath. `if` and `in` are not applied: the viewer shows every observation.
    inline constexpr const char* kBrowseProgram = R"ADO(*! Callisto: the data viewer of the application running Stata (there is no Data Editor here)
program define @NAME@
    version 17
    syntax [varlist(default=none)] [if] [in] [, *]
    if `"`if'`in'"' != "" {
        display as text "(the data viewer shows every observation: if and in are not applied)"
    }
    global CALLISTO_VIEW `"`varlist'"'
    global CALLISTO_VIEW_ON 1
end
)ADO";

    // The commands the program above is written as
    inline constexpr const char* kBrowseNames[] = { "browse", "br", "bro", "brow", "brows", "edit", "ed", "edi" };

} }

#endif
