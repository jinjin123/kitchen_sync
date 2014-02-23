require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class HashFromTest < KitchenSync::EndpointTestCase
  include TestTableSchemas

  def from_or_to
    :from
  end

  def setup_with_footbl
    clear_schema
    create_footbl
    execute "INSERT INTO footbl VALUES (2, 10, 'test'), (4, NULL, 'foo'), (5, NULL, NULL), (8, -1, 'longer str'), (100, 0, 'last')"
    @rows = [["2",  "10",       "test"],
             ["4",   nil,        "foo"],
             ["5",   nil,          nil],
             ["8",  "-1", "longer str"],
             ["100", "0",       "last"]]
    @keys = @rows.collect {|row| [row[0]]}
    send_handshake_commands
  end

  test_each "calculates the hash of all the rows whose key is greater than the first argument and not greater than the last argument, and if it matches, responds likewise with the hash of the next rows (doubling the count of rows hashed)" do
    setup_with_footbl
    assert_equal [Commands::HASH_NEXT, [], @keys[0], hash_of(@rows[0..0])],
     send_command(Commands::OPEN, "footbl")

    assert_equal([Commands::HASH_NEXT, @keys[1], @keys[3], hash_of(@rows[2..3])],
     send_command(Commands::HASH_NEXT, @keys[0], @keys[1], hash_of(@rows[1..1])))

    assert_equal([Commands::HASH_NEXT, @keys[2], @keys[4], hash_of(@rows[3..4])],
     send_command(Commands::HASH_NEXT, @keys[0], @keys[2], hash_of(@rows[1..2])))
  end

  test_each "starts from the first row if an empty array is given as the first argument" do
    setup_with_footbl
    assert_equal [Commands::HASH_NEXT, [], @keys[0], hash_of(@rows[0..0])],
     send_command(Commands::OPEN, "footbl")

    assert_equal([Commands::HASH_NEXT, @keys[0], @keys[2], hash_of(@rows[1..2])],
     send_command(Commands::HASH_NEXT, [], @keys[0], hash_of(@rows[0..0])))

    assert_equal([Commands::HASH_NEXT, @keys[1], @keys[4], hash_of(@rows[2..4])],
     send_command(Commands::HASH_NEXT, [], @keys[1], hash_of(@rows[0..1])))
  end

  test_each "sends back an empty rowset for the key range greater than the last row's key if the hash of the last row is given and matches" do
    setup_with_footbl
    assert_equal [Commands::HASH_NEXT, [], @keys[0], hash_of(@rows[0..0])],
     send_command(Commands::OPEN, "footbl")

    assert_equal([Commands::ROWS, @keys[-1], []],
     send_command(Commands::HASH_NEXT, @keys[-2], @keys[-1], hash_of(@rows[-1..-1])))
  end

  test_each "sends back an empty rowset for the key range greater than the last row's key if the hash of the last set of rows is given and matches" do
    setup_with_footbl
    assert_equal [Commands::HASH_NEXT, [], @keys[0], hash_of(@rows[0..0])],
     send_command(Commands::OPEN, "footbl")

    assert_equal([Commands::ROWS, @keys[-1], []],
     send_command(Commands::HASH_NEXT, @keys[-4], @keys[-1], hash_of(@rows[-3..-1])))
  end

  test_each "sends back its hash of half as many rows if the hash of multiple rows is given and it doesn't match, keeping track of the failed range end" do
    setup_with_footbl
    assert_equal [Commands::HASH_NEXT, [], @keys[0], hash_of(@rows[0..0])],
     send_command(Commands::OPEN, "footbl")

    assert_equal([Commands::HASH_FAIL, @keys[0], @keys[1], @keys[2], hash_of(@rows[1..1])],
     send_command(Commands::HASH_NEXT, @keys[0], @keys[2], hash_of(@rows[1..2]).reverse))

    assert_equal([Commands::HASH_FAIL, @keys[0], @keys[2], @keys[4], hash_of(@rows[1..2])],
     send_command(Commands::HASH_NEXT, @keys[0], @keys[4], hash_of(@rows[1..4]).reverse))
  end

  test_each "sends back the row instead if the hash of only one is given and it doesn't match" do
    setup_with_footbl
    assert_equal [Commands::HASH_NEXT, [], @keys[0], hash_of(@rows[0..0])],
     send_command(Commands::OPEN, "footbl")

    assert_equal([Commands::ROWS_AND_HASH_NEXT, @keys[0], @keys[1], @keys[2], hash_of(@rows[2..2])],
     send_command(Commands::HASH_NEXT, @keys[0], @keys[1], hash_of(@rows[1..1]).reverse))
    assert_equal @rows[1], unpack_next
    assert_equal       [], unpack_next # indicates end - see rows_from_test.rb

    assert_equal([Commands::ROWS_AND_HASH_NEXT, [], @keys[0], @keys[1], hash_of(@rows[1..1])],
     send_command(Commands::HASH_NEXT, [], @keys[0], hash_of(@rows[0..0]).reverse))
    assert_equal @rows[0], unpack_next
    assert_equal       [], unpack_next # as above
  end

  test_each "sends multiple initial rows if their data size is approximately between half and the full target block size" do
    clear_schema
    create_texttbl
    execute "INSERT INTO texttbl VALUES (1, '#{'x'*30*1024}'), (2, '#{'x'*30*1024}'), (3, '#{'x'*30*1024}')"

    @rows = [["1", "x"*30*1024],
             ["2", "x"*30*1024],
             ["3", "x"*30*1024]]
    @keys = @rows.collect {|row| [row[0]]}

    send_handshake_commands(64*1024)

    assert_equal [Commands::HASH_NEXT, [], @keys[1], hash_of(@rows[0..1])],
     send_command(Commands::OPEN, "texttbl")
  end

  test_each "sends single initial rows if its data size is approximately between half and the full target block size" do
    clear_schema
    create_texttbl
    execute "INSERT INTO texttbl VALUES (1, '#{'x'*60*1024}'), (2, '#{'x'*60*1024}'), (3, '#{'x'*60*1024}')"

    @rows = [["1", "x"*60*1024],
             ["2", "x"*60*1024],
             ["3", "x"*60*1024]]
    @keys = @rows.collect {|row| [row[0]]}

    send_handshake_commands(64*1024)

    assert_equal [Commands::HASH_NEXT, [], @keys[0], hash_of(@rows[0..0])],
     send_command(Commands::OPEN, "texttbl")
  end

  test_each "sends back the rows instead of the hash of multiple rows is given and it doesn't match, but the range has less than approximately the target block size of data" do
    clear_schema
    create_texttbl
    execute "INSERT INTO texttbl VALUES (1, '#{'x'*20*1024}'), (2, '#{'x'*20*1024}'), (3, '#{'x'*20*1024}'), (4, '#{'x'*20*1024}'), (5, '#{'x'*80*1024}'), (6, '#{'x'*80*1024}'), (7, '#{'x'*80*1024}')"

    @rows = [["1", "x"*20*1024],
             ["2", "x"*20*1024],
             ["3", "x"*20*1024],
             ["4", "x"*20*1024],
             ["5", "x"*80*1024],
             ["6", "x"*80*1024],
             ["7", "x"*80*1024]]
    @keys = @rows.collect {|row| [row[0]]}
    send_handshake_commands(64*1024)

    assert_equal [Commands::HASH_NEXT, [], @keys[1], hash_of(@rows[0..1])],
     send_command(Commands::OPEN, "texttbl")

    assert_equal([Commands::ROWS_AND_HASH_NEXT, @keys[1], @keys[3], @keys[4], hash_of(@rows[4..4])],
     send_command(Commands::HASH_NEXT, @keys[1], @keys[3], hash_of(@rows[2..3]).reverse))
    assert_equal @rows[2], unpack_next
    assert_equal @rows[3], unpack_next
    assert_equal       [], unpack_next

    assert_equal([Commands::HASH_FAIL, @keys[4], @keys[5], @keys[6], hash_of(@rows[5..5])],
     send_command(Commands::HASH_NEXT, @keys[4], @keys[6], hash_of(@rows[5..6]).reverse))
  end

  test_each "supports composite keys" do
    clear_schema
    create_secondtbl
    execute "INSERT INTO secondtbl VALUES (2349174, 'xy', 1, 2), (968116383, 'aa', 9, 9), (100, 'aa', 100, 100), (363401169, 'ab', 20, 340)"
    @rows = [[      "100", "aa", "100", "100"], # first because the second column is the first term in the key so it's sorted like ["aa", 100]
             ["968116383", "aa",   "9",   "9"],
             ["363401169", "ab",  "20", "340"],
             [  "2349174", "xy",   "1",   "2"]]
    # note that the primary key columns are in reverse order to the table definition; the command arguments need to be given in the key order, but the column order for the results is unrelated
    @keys = @rows.collect {|row| [row[1], row[0]]}
    send_handshake_commands
    assert_equal [Commands::HASH_NEXT, [], @keys[0], hash_of(@rows[0..0])],
     send_command(Commands::OPEN, "secondtbl")

    assert_equal([Commands::HASH_NEXT, @keys[0], @keys[2], hash_of(@rows[1..2])],
     send_command(Commands::HASH_NEXT,       [], @keys[0], hash_of(@rows[0..0])))

    assert_equal([Commands::HASH_NEXT, @keys[1], @keys[3], hash_of(@rows[2..3])],
     send_command(Commands::HASH_NEXT, ["aa", "101"], @keys[1], hash_of(@rows[1..1])))

    assert_equal([Commands::HASH_NEXT, ["aa", "101"], @keys[2], hash_of(@rows[1..2])],
     send_command(Commands::HASH_NEXT,       [], ["aa", "101"], hash_of(@rows[0..0])))

    assert_equal([Commands::HASH_NEXT, @keys[1], @keys[3], hash_of(@rows[2..3])],
     send_command(Commands::HASH_NEXT, @keys[0], @keys[1], hash_of(@rows[1..1])))

    assert_equal([Commands::HASH_NEXT, @keys[2], @keys[3], hash_of(@rows[3..3])],
     send_command(Commands::HASH_NEXT, @keys[0], @keys[2], hash_of(@rows[1..2])))

    assert_equal([Commands::ROWS_AND_HASH_NEXT, @keys[0], @keys[1], @keys[2], hash_of(@rows[2..2])],
     send_command(Commands::HASH_NEXT, @keys[0], @keys[1], hash_of(@rows[1..1]).reverse))
    assert_equal @rows[1], unpack_next
    assert_equal       [], unpack_next # indicates end - see rows_from_test.rb

    assert_equal([Commands::ROWS_AND_HASH_NEXT, @keys[0], @keys[1], @keys[2], hash_of(@rows[2..2])],
     send_command(Commands::HASH_NEXT, @keys[0], ["aa", "101"], hash_of(@rows[1..1])))
    assert_equal @rows[1], unpack_next
    assert_equal       [], unpack_next
  end
end
