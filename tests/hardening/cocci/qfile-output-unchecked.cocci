// A file a build tool writes for something else to install goes through
// QSaveFile and its commit(), not a plain QFile: QFile buffers the writes
// and its destructor flushes them without reporting a failure, so a full
// disk leaves a short file and a tool that exits 0. generate_qmap did this,
// and Qt refused the short keymap at boot. SmPL cannot match the open
// mode (QIODevice::WriteOnly), so any QFile opened in a tool is flagged;
// a tool that reads one lists it in baseline.txt.
@@
typedef QFile;
QFile f;
expression M;
@@
* f.open(M)
