-- Test gprecoverseg from config file uses the correct dbid.
--
-- In github issue 9837 dbid in gp_segment_configuration is not
-- consistent with dbid in file internal.auto.conf.
-- This is caused by gprecoverseg fetch the smallest dbid in
-- gp_segment_configuration which is not occupied by others when
-- adding a new mirror. When dbid in gp_segment_configuration is not
-- continous, the inconsistent issue will happen

include: helpers/server_helpers.sql;

--
-- generate_recover_config_file:
--   generate config file used by recoverseg -i
--
create or replace function generate_recover_config_file(datadir text, port text)
returns void as $$
    import io
    import os
    myhost = os.uname()[1]
    inplaceConfig = myhost + '|' + port + '|' + datadir
    configStr = inplaceConfig + ' ' + inplaceConfig
	
    f = open("/tmp/recover_config_file", "w")
    f.write(configStr)
    f.close()
$$ language plpythonu;

SELECT dbid, role, preferred_role, content, mode, status FROM gp_segment_configuration order by dbid;
-- stop a primary in order to trigger a mirror promotion
select pg_ctl((select datadir from gp_segment_configuration c
where c.role='p' and c.content=1), 'stop');

-- trigger failover
select gp_request_fts_probe_scan();

-- wait for content 1 (earlier mirror, now primary) to finish the promotion
1U: select 1;
-- Quit this utility mode session, as need to start fresh one below
1Uq:

-- make the dbid in gp_segment_configuration not continuous
-- dbid=2 corresponds to content 0 and role p, change it to dbid=9
set allow_system_table_mods to true;
update gp_segment_configuration set dbid=9 where content=0 and role='p';

-- trigger failover
select gp_request_fts_probe_scan();

-- wait for content 0 (earlier mirror, now primary) to finish the promotion
0U: select 1;
-- Quit this utility mode session, as need to start fresh one below
0Uq:

-- generate recover config file
select generate_recover_config_file(
	(select datadir from gp_segment_configuration c where c.role='m' and c.content=1),
	(select port from gp_segment_configuration c where c.role='m' and c.content=1)::text);

-- recover from config file, only seg with content=1 will be recovered
!\retcode gprecoverseg -a -i /tmp/recover_config_file;

-- after gprecoverseg -i, the down segemnt should be up
-- in mirror mode
select status from gp_segment_configuration
where role='m' and content=1;

-- recover should reuse the old dbid and not occupy dbid=2
select dbid from gp_segment_configuration where dbid=2;

update gp_segment_configuration set dbid=2 where dbid=9;
set allow_system_table_mods to false;

-- we manually change dbid from 2 to 9, which causes the
-- corresponding segment down as well, so recovery full
-- at here
!\retcode gprecoverseg -a;

-- loop while segments come in sync
do $$
 begin /* in func */
    for i in 1..120 loop /* in func */
      if (select count(*) = 0 from gp_segment_configuration where content != -1 and mode != 's') then /* in func */
        return; /* in func */
      end if; /* in func */
      perform gp_request_fts_probe_scan(); /* in func */
    end loop; /* in func */
  end; /* in func */
$$;

-- rebalance the cluster
!\retcode gprecoverseg -ar;

-- loop while segments come in sync
do $$
 begin /* in func */
    for i in 1..120 loop /* in func */
      if (select count(*) = 0 from gp_segment_configuration where content != -1 and mode != 's') then /* in func */
        return; /* in func */
      end if; /* in func */
      perform gp_request_fts_probe_scan(); /* in func */
    end loop; /* in func */
  end; /* in func */
$$;

-- recheck gp_segment_configuration after rebalance
SELECT dbid, role, preferred_role, content, mode, status FROM gp_segment_configuration order by dbid;

-- remove the config file
!\retcode rm /tmp/recover_config_file