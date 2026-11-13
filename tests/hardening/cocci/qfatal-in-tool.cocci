// qFatal() aborts: the build log gets a core dump instead of an error, and
// whatever the tool had half written stays. A tool reports and returns
// non-zero.
@@
expression list L;
@@
* qFatal(L);
