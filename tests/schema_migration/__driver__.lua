
-- This test ensures that 'monotone migrate' can take any old-format
-- database and bring it forward to the current time; it is basically a
-- compatibility test.  We actually don't test against old-format
-- databases directly, because some old-format databases can't be read
-- at all by a modern monotone -- you have to do a dump/load first.  So
-- instead we store pre-dumped old-format databases.  So technically
-- we're not checking that 'db migrate' can handle things, we're just
-- checking that 'dump | load; db migrate' can handle things.  But that
-- should be good enough.

-- This means that every time the database schema is changed, you need
-- to add a new piece to this test, for the new schema.  The way you do
-- this is to run this test with the -d option, like so:
--   $ ./testsuite -d schema_migration
-- this will cause autotest to leave behind the temporary files the test
-- generates. You want 'tester_dir/schema_migration/<something>.db.dumped'.
-- Copy that file to this directory, and add a line
--   check_migrate_from("<something>")
-- to the end of this file, with the <something> being the same <something>
-- that was in the filename (it's the schema id, if you were wondering).

--------------------------------------------------------------------------------------------------------------------------------------------
---- Do not touch this code; you'll have to regenerate all the test
---- databases if you do!
--------------------------------------------------------------------------------------------------------------------------------------------

mtn_setup()
-- We don't want the standard db, we want full control ourselves
remove("test.db")
remove("keys")
check(mtn("db", "init"))

-- Put some random keys in, with and without corresponding private keys
get("migrate_keys", "stdin")
check(mtn("read"), 0, false, false, true)

addfile("testfile1", "f1v1\n")
addfile("testfile2", "f2v1\n")
check(mtn("attr", "set", "testfile1", "testfile1_key", "initial_value"), 0, false, false)
check(mtn("attr", "set", ".", "dir_key", "initial_value"), 0, false, false)
check(mtn("commit", "--branch=testbranch1", "--date=1999-01-01T12:00:00", "--message=blah-blah"), 0, false, false)
rev = base_revision()

check(mtn("cert", rev, "somekey", "somevalue"), 0, false, false)

writefile("testfile1", "f1v2\n")
addfile("testfile3", "f3v1\n")
check(mtn("attr", "drop", "testfile1", "testfile1_key"), 0, false, false)
check(mtn("attr", "set", ".", "dir_key", "new_value"), 0, false, false)
check(mtn("commit", "--branch=testbranch2", "--date=2000-01-01T12:00:00", "--message=blah-blah"), 0, false, false)

revert_to(rev)

writefile("testfile2", "f2v2\n")
addfile("testfile4", "f4v1\n")
check(mtn("commit", "--branch=testbranch1", "--date=2001-01-01T12:00:00", "--message=blah-blah"), 0, false, false)

check(mtn("propagate", "--date=2002-01-01T12:00:00", "testbranch2", "testbranch1"), 0, false, false)
check(mtn("update"), 0, false, false)

check(mtn("drop", "testfile1"), 0, false, false)
writefile("testfile4", "f4v2\n")
check(mtn("commit", "--branch=testbranch3", "--date=2003-01-01T12:00:00", "--message=blah-blah"), 0, false, false)

rename("test.db", "latest.mtn")

if debugging then
  check(mtn("--db=latest.mtn", "db", "dump"), 0, true, false)
  rename("stdout", "latest.mtn.dumped")
  check(mtn("--db=latest.mtn", "db", "version"), 0, true, false)
  local ver = string.gsub(readfile("stdout"), "^.*: (.*)%s$", "%1")
  rename("latest.mtn.dumped", ver..".mtn.dumped")
end

--------------------------------------------------------------------------------------------------------------------------------------------
---- End untouchable code
--------------------------------------------------------------------------------------------------------------------------------------------

function check_migrate_from(id)
  -- id.dumped is a 'db dump' of a db with schema "id"
  get(id..".mtn.dumped", "stdin")
  check(mtn("--db="..id..".mtn", "db", "load"), 0, false, false, true)
  -- check that the version's correct
  check(mtn("--db="..id..".mtn", "db", "version"), 0, true, false)
  check(qgrep(id, "stdout"))
  -- migrate it
  check(mtn("--db="..id..".mtn", "db", "migrate"), 0, false, false)
  check_same_db_contents(id..".mtn", "latest.mtn")
end

check_migrate_from("1db80c7cee8fa966913db1a463ed50bf1b0e5b0e")
check_migrate_from("9d2b5d7b86df00c30ac34fe87a3c20f1195bb2df")