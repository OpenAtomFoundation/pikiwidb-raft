# Pikiwidb does not support the docs command

start_server {tags {"command"}} {
    test "Command docs supported." {
        set doc [r command docs set]
        # puts $doc
        assert [dict exists $doc set]
    }
}
