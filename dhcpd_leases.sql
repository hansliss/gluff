--
-- Table structure for table `cids`
--

CREATE TABLE `cids` (
  `id` int(11) NOT NULL auto_increment,
  `value` varchar(63) default NULL,
  PRIMARY KEY  (`id`)
);

--
-- Table structure for table `hws`
--

CREATE TABLE `hws` (
  `id` int(11) NOT NULL auto_increment,
  `value` varchar(63) default NULL,
  PRIMARY KEY  (`id`)
);

--
-- Table structure for table `ips`
--

CREATE TABLE `ips` (
  `id` int(11) NOT NULL auto_increment,
  `value` varchar(63) default NULL,
  PRIMARY KEY  (`id`)
);

--
-- Table structure for table `leases`
--

CREATE TABLE `leases` (
  `ip` int(11) NOT NULL default '0',
  `start` datetime NOT NULL default '0000-00-00 00:00:00',
  `end` datetime default NULL,
  `hw` int(11) default NULL,
  `cid` int(11) default NULL,
  `rid` int(11) default NULL,
  PRIMARY KEY  (`ip`,`start`,`cid`,`rid`)
);

--
-- Table structure for table `rids`
--

CREATE TABLE `rids` (
  `id` int(11) NOT NULL auto_increment,
  `value` varchar(63) default NULL,
  PRIMARY KEY  (`id`)
);
