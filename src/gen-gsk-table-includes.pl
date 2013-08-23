#! /usr/bin/perl -w

for my $simplify (0, 1)
{
  for my $flush (0, 1)
  {
    for my $has_len (0, 1)
    {
      for my $memcmp (0, 1)
      {
	for my $has_merge (0, 1)
	{
print <<"EOF";
#define DO_SIMPLIFY $simplify
#define DO_FLUSH    $flush
#define HAS_LEN     $has_len
#define USE_MEMCMP  $memcmp
#define HAS_MERGE   $has_merge
EOF
print "#define MANGLE_FUNC_NAME(basename) basename ## __";
print ((qw(nosimplify simplify))[$simplify], "_",
      (qw(noflush flush))[$flush], "_",
      (qw(nolen haslen))[$has_len], "_",
      (qw(compare memcmp))[$memcmp], "_",
      (qw(nomerge merge))[$has_merge]);
print "\n#include \"gsktable-implement-run-merge-task.inc.c\"\n";
}}}}}
