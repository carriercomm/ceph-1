  $ radosgw_admin --help
  usage: radosgw_admin <cmd> [options...]
  commands:
    user create                create a new user
    user modify                modify user
    user info                  get user info
    user rm                    remove user
    user suspend               suspend a user
    user enable                reenable user after suspension
    subuser create             create a new subuser
    subuser modify             modify subuser
    subuser rm                 remove subuser
    key create                 create access key
    key rm                     remove access key
    buckets list               list buckets
    bucket link                link bucket to specified user
    bucket unlink              unlink bucket from specified user
    pool info                  show pool information
    pool create                generate pool information (requires bucket)
    policy                     read bucket/object policy
    log show                   dump a log from specific object or (bucket + date
                               + pool-id)
    temp remove                remove temporary objects that were created up to
                               specified date (and optional time)
  options:
     --uid=<id>                user id
     --subuser=<name>          subuser name
     --access-key=<key>        S3 access key
     --os-user=<group:name>    OpenStack user
     --email=<email>
     --auth_uid=<auid>         librados uid
     --secret=<key>            S3 key
     --os-secret=<key>         OpenStack key
     --gen-access-key          generate random access key
     --gen-secret              generate random secret key
     --access=<access>         Set access permissions for sub-user, should be one
                               of read, write, readwrite, full
     --display-name=<name>
     --bucket=<bucket>
     --object=<object>
     --date=<yyyy-mm-dd>
     --time=<HH:MM:SS>
     --pool-id=<pool-id>
     --format=<format>         specify output format for certain operations: xml,
                               json
     --purge-data              when specified, user removal will also purge all the
                               user data
  --conf/-c        Read configuration from the given configuration file
  -d               Run in foreground, log to stderr.
  -f               Run in foreground, log to usual location.
  --id/-i          set ID portion of my name
  --name/-n        set name (TYPE.ID)
  --version        show version and quit
  
  [1]
