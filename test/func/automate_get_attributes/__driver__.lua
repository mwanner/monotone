
mtn_setup()

-- add a file without attributes
addfile("testfile", "foo")
commit("mainbranch")

-- at first check for the version on the file w/o attributes
check(mtn("automate", "get_attributes", "testfile"), 0, true, true)
check(fsize("stderr") == 0)
check(fsize("stdout") == 0)

-- add three test attributes and commit them
check(mtn("attr", "set", "testfile", "key1", "persists"), 0, false, false)
check(mtn("attr", "set", "testfile", "key2", "will_be_dropped"), 0, false, false)
check(mtn("attr", "set", "testfile", "key3", "will_be_changed"), 0, false, false)
commit("mainbranch");

-- now add another one, drop one of the existing and
-- re-add one of the existing
check(mtn("attr", "set", "testfile", "key4", "has_been_added"), 0, false, false)
check(mtn("attr", "drop", "testfile", "key3"), 0, false, false)
check(mtn("attr", "set", "testfile", "key3", "has_been_changed"), 0, false, false)
check(mtn("attr", "drop", "testfile", "key2"), 0, false, false)

-- the actual check of the interface
check(mtn("automate", "get_attributes", "testfile"), 0, true, true)
check(fsize("stderr") == 0)
parsed = parse_basic_io(readfile("stdout"))
-- make sure the output generated 8 stanzas
check(#parsed == 8)
lastkey = ""
checked = {}
for _,l in pairs(parsed) do
    if l.name == "attr" then
        lastkey = l.values[1]
        val = l.values[2]
        if lastkey == "key1" then check(val == "persists") end
        if lastkey == "key2" then check(val == "will_be_dropped") end
        if lastkey == "key3" then check(val == "has_been_changed") end
        if lastkey == "key4" then check(val == "has_been_added") end
    end
    if l.name == "state" then
        state = l.values[1]

        if lastkey == "key1" then
            check(state == "unchanged")
            checked[lastkey] = true
        end
        if lastkey == "key2" then
            check(state == "dropped")
            checked[lastkey] = true
        end
        if lastkey == "key3" then
            check(state == "changed")
            checked[lastkey] = true
        end
        if lastkey == "key4" then
            check(state == "added")
            checked[lastkey] = true
        end
    end
end

check(checked["key1"] and checked["key2"] and checked["key3"] and checked["key4"])

commit("mainbranch")

-- check that dropped attributes do not popup in further revisions
check(mtn("automate", "get_attributes", "testfile"), 0, true, true)
check(fsize("stderr") == 0)
parsed = parse_basic_io(readfile("stdout"))

for _,l in pairs(parsed) do
    if l.name == "attr" then
        curkey = l.values[1]
        check(curkey ~= "key2")
    end
end

-- check that new attributes which resemble the name of previously
-- dropped attributes are correctly listed as added, and not changed
-- (bug in 0.35)
check(mtn("attr", "set", "testfile", "key2", "new_value"), 0, false, false)
check(mtn("automate", "get_attributes", "testfile"), 0, true, true)
check(fsize("stderr") == 0)
parsed = parse_basic_io(readfile("stdout"))

curkey = ""
for _,l in pairs(parsed) do
    if l.name == "attr" then
        curkey = l.values[1]
    end
    if l.name == "state" and curkey == "key2" then
        state = l.values[1]
        check(state == "added")
    end
end

-- check that we can query arbitrary attributes from earlier revisions
check(mtn("automate", "get_attributes", "-r", "0123456789012345678901234567890123456789", "bla"), 1, false, true)
check(qgrep("no revision 0123456789012345678901234567890123456789 found in database", "stderr"))

rev = base_revision()
check(mtn("automate", "get_attributes", "foo", "-r", rev), 1, false, true)
check(qgrep("unknown path 'foo' in " .. rev, "stderr"))

check(mtn("automate", "get_attributes", "testfile", "-r", rev), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))
check(#parsed == 6)

lastkey = ""
checked = {}
for _,l in pairs(parsed) do
    if l.name == "attr" then
        lastkey = l.values[1]
        val = l.values[2]
        if lastkey == "key1" then check(val == "persists")
        elseif lastkey == "key3" then check(val == "has_been_changed")
        elseif lastkey == "key4" then check(val == "has_been_added")
        else check(false) end
    end
    if l.name == "state" then
        check(l.values[1] == "unchanged")
        checked[lastkey] = true
    end
end

check(checked["key1"] and checked["key3"] and checked["key4"])

