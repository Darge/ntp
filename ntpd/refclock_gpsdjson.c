/*
 * refclock_gpsdjson.c - clock driver as GPSD JSON client
 *	Juergen Perlinger (perlinger@ntp.org) Feb 11, 2014
 *	inspired by refclock_nmea.c
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_GPSDJSON)


/* =====================================================================
 * get the little JSMN library directly into our guts
 */
#include "../libjsmn/jsmn.c"

/* =====================================================================
 * header stuff we need
 */
#include "ntp_types.h"
#include <stdio.h>
//#include <ctype.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <poll.h>

//#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
//#include <sys/epoll.h>
#include <sys/stat.h>


#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_unixtime.h"
#include "ntp_refclock.h"
#include "ntp_stdlib.h"
#include "ntp_calendar.h"
#include "timespecops.h"

#define	PRECISION	(-9)	/* precision assumed (about 2 ms) */
#define	PPS_PRECISION	(-20)	/* precision assumed (about 1 us) */
#define	REFID		"GPSD"	/* reference id */
#define	DESCRIPTION	"GPSD JSON client clock" /* who we are */

#define MAX_PDU_LEN	1600
#define TICKOVER_MAX	10

#ifndef BOOL
# define BOOL int
#endif
#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

/* =====================================================================
 * We use the same device name scheme as does the NMEA driver; since
 * GPSD supports the same links, we can select devices by a fixed name.
 */
static const char * s_dev_stem = "/dev/gps";

/* =====================================================================
 * forward declarations for transfer vector and the vector itself
 */

static	void	gpsd_init	(void);
static	int	gpsd_start	(int, struct peer *);
static	void	gpsd_shutdown	(int, struct peer *);
static	void	gpsd_receive	(struct recvbuf *);
static	void	gpsd_poll	(int, struct peer *);
static	void	gpsd_control	(int, const struct refclockstat *,
				 struct refclockstat *, struct peer *);
static	void	gpsd_timer	(int, struct peer *);

struct refclock refclock_gpsdjson = {
	gpsd_start,		/* start up driver */
	gpsd_shutdown,		/* shut down driver */
	gpsd_poll,		/* transmit poll message */
	gpsd_control,		/* fudge control */
	gpsd_init,		/* initialize driver */
	noentry,		/* buginfo */
	gpsd_timer		/* called once per second */
};

/* =====================================================================
 * our local clock unit and data
 */
struct gpsd_unit {
	/* current line protocol version */
	uint16_t proto_major;
	uint16_t proto_minor;

	/* PULSE time stamps */
	l_fp pps_stamp;	/* effective PPS time stamp */
	l_fp pps_recvt;	/* when we received the PPS message */

	/* TPV (GPS) time stamps */
	l_fp tpv_stamp;	/* effective GPS time stamp */
	l_fp tpv_recvt;	/* whn we received the TPV message */

	/* Flags to indicate available data */
	int fl_tpv : 1;	/* valid TPV seen (have time) */
	int fl_pps : 1;	/* valid pulse seen */
	int fl_ver : 1;	/* have protocol version */

	/* admin stuff for sockets and device selection */
	struct pollfd     pfd;		/* for non-blocking connect */
	struct addrinfo * addr;		/* next address to try */
	int               tickover;	/* timeout countdown */
	char            * device;	/* device name of unit */

	/* record assemby buffer and saved length */
	int  buflen;
	char buffer[MAX_PDU_LEN];
};

/* =====================================================================
 * our local parser context
 */
#define JSMN_MAXTOK	100

struct json_ctx {
	char        * buf;
	int           ntok;
	jsmntok_t     tok[JSMN_MAXTOK];
};

typedef struct json_ctx json_ctx;
typedef int tok_ref;
#define INVALID_TOKEN (-1)

static BOOL json_parse_record(json_ctx * ctx, char * buf);
static int  json_object_lookup(const json_ctx *ctx, tok_ref tok, const char * key);

/* =====================================================================
 * static local helpers forward decls
 */
static void gpsd_init_socket(struct refclockproc * const pp,
			     struct gpsd_unit    * const up);
static void gpsd_test_socket(struct refclockproc * const pp,
			     struct gpsd_unit    * const up);
static void gpsd_stop_socket(struct refclockproc * const pp,
			     struct gpsd_unit    * const up);
static void gpsd_parse(struct peer * const peer,
		       const l_fp  * const rtime);
static BOOL convert_time(l_fp * fp, const char * gps_time);
static void save_ltc(struct refclockproc * const pp, const char * const tc);


/* =====================================================================
 * JSON parsing utils
 */
static tok_ref json_token_skip(const json_ctx * ctx, tok_ref tid);
static tok_ref json_object_lookup(const json_ctx * ctx,	tok_ref tid,
				  const char * key);
static const char* json_object_lookup_string(const json_ctx * ctx,
					     tok_ref tid, const char * key);
#ifdef HAVE_LONG_LONG
static long long json_object_lookup_int(const json_ctx * ctx,
					tok_ref tid, const char * key);
#else
# error Blooper! we need some 64-bit integer...
#endif
static double json_object_lookup_float(const json_ctx * ctx,
				       tok_ref tid, const char * key);
static BOOL json_parse_record(json_ctx * ctx, char * buf);

/* =====================================================================
 * local / static stuff
 */

/* The logon string is actually the ?WATCH command of GPSD, using JSON
 * data and selecting the GPS device name we created from our unit
 * number. [Note: This is a format string!]
 */
static const char * s_logon =
    "?WATCH={\"enable\":true,\"json\":true,\"device\":\"%s\"};\r\n";

/* We keep a static list of network addresses for 'localhost:gpsd', and
 * we try to connect to them in round-robin fashion.
 */
static struct addrinfo * s_gpsd_addr;

/* =====================================================================
 * the clock functions
 */

/* ---------------------------------------------------------------------
 * Init: This currently just gets the socket address for the GPS daemon
 */
static void
gpsd_init(void)
{
	struct addrinfo hints;
	
	memset(&hints, 0, sizeof(hints));

	//hints.ai_family   = AF_UNSPEC;
	hints.ai_family   = AF_INET;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_socktype = SOCK_STREAM;

	/* just take the first configured address of localhost... */
	if (getaddrinfo("localhost", "gpsd", &hints, &s_gpsd_addr))
		s_gpsd_addr = NULL;
}

/* ---------------------------------------------------------------------
 * Start: allocate a unit pointer and set up the runtime data
 */

static int
gpsd_start(
	int           unit,
	struct peer * peer)
{
	struct refclockproc * const pp = peer->procptr;
	struct gpsd_unit * const    up = emalloc_zero(sizeof(*up));

	struct stat sb;

	UNUSED_ARG(unit);

	up->pfd.fd     = -1;
	up->pfd.events = POLLIN;
	up->addr       = s_gpsd_addr;

	/* Allocate and initialize unit structure */
	pp->unitptr = (caddr_t)up;
	pp->io.fd = -1;
	pp->io.clock_recv = gpsd_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->a_lastcode[0] = '\0';

	/* Initialize miscellaneous variables */
	peer->precision = PRECISION;
	pp->clockdesc   = DESCRIPTION;
	memcpy(&pp->refid, REFID, 4);

	/* Create the device name and check for a Character Device. It's
	 * assumed that GPSD was started with the same link, so the
	 * names match. (If this is not practicable, we will have to
	 * read the symlink, if any, so we can get the true device
	 * file.)
	 */
	if (-1 == asprintf(&up->device, "%s%u", s_dev_stem, unit)) {
	    up->device = NULL; /* undefined if asprintf fails... */
	    /*TODO: logging*/
	    goto dev_fail;
	}
	if (-1 == stat(up->device, &sb)) {
	    /*TODO: logging */
	    goto dev_fail;
	}
	if (!S_ISCHR(sb.st_mode)) {
	    /*TODO: logging */
	    goto dev_fail;
	}

	return TRUE;

dev_fail:
	/* On failure, remove all UNIT ressources and declare defeat. */
	if (up) {
	    free(up->device);
	    free(up);
	}
	pp->unitptr = (caddr_t)NULL;
	return FALSE;
}

/* ------------------------------------------------------------------ */

static void
gpsd_shutdown(
	int           unit,
	struct peer * peer)
{
	struct refclockproc * const pp = peer->procptr;
	struct gpsd_unit    * const up = (struct gpsd_unit *)pp->unitptr;

	UNUSED_ARG(unit);

	if (up) {
	    free(up->device);
	    free(up);
	}
	pp->unitptr = (caddr_t)NULL;
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);
	pp->io.fd = -1;
}

/* ------------------------------------------------------------------ */

static void
gpsd_receive(
	struct recvbuf * rbufp)
{
	/* declare & init control structure ptrs */
	struct peer	    * const peer = rbufp->recv_peer;
	struct refclockproc * const pp = peer->procptr;
	struct gpsd_unit    * const up = (struct gpsd_unit*)pp->unitptr;
	
	const char *psrc, *esrc;
	char       *pdst, *edst, ch;

	/* Since we're getting a raw stream data, we must assemble lines
	 * in our receive buffer. We can't use neither 'refclock_gtraw'
	 * not 'refclock_gtlin' here...  We process chars until we reach
	 * an EoL (that is, line feed) but we truncate the message if it
	 * does not fit the buffer.  GPSD might truncate messages, too,
	 * so dealing with truncated buffers is necessary anyway.
	 */
	psrc = (const char*)rbufp->recv_buffer;
	esrc = psrc + rbufp->recv_length;

	pdst = up->buffer + up->buflen;
	edst = pdst + sizeof(up->buffer) - 1; /* for trailing NUL */

	while (psrc != esrc) {
		ch = *psrc++;
		if (ch == '\n') {
			/* trim trailing whitespace & terminate buffer */
			while (pdst != up->buffer && pdst[-1] <= ' ')
				--pdst;
			*pdst = '\0';
			/* process data and reset buffer */
			gpsd_parse(peer, &rbufp->recv_time);
			pdst = up->buffer;
		} else if (pdst != edst) {
			/* add next char, ignoring leading whitespace */
			if (ch > ' ' || pdst != up->buffer)
				*pdst++ = ch;
		}
	}
	up->buflen   = pdst - up->buffer;
	up->tickover = TICKOVER_MAX;
}

/* ------------------------------------------------------------------ */

static void
gpsd_poll(
	int           unit,
	struct peer * peer)
{
	struct refclockproc * const pp = peer->procptr;

	/* If the median filter is empty, claim a timeout; otherwise
	 * process the input data and keep the stats going.
	 */
	if (pp->coderecv == pp->codeproc) {
		peer->flags    &= ~FLAG_PPS;
		peer->precision = PRECISION;
		refclock_report(peer, CEVNT_TIMEOUT);
	} else {
		pp->polls++;
		pp->lastref = pp->lastrec;
		refclock_receive(peer);
	}
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
}

/* ------------------------------------------------------------------ */

static void
gpsd_control(
	int                         unit,
	const struct refclockstat * in_st,
	struct refclockstat       * out_st,
	struct peer               * peer  )
{
	/* Not yet implemented -- do we need it? */
}

/* ------------------------------------------------------------------ */

static void
gpsd_timer(
	int           unit,
	struct peer * peer)
{
	struct refclockproc * const pp = peer->procptr;
	struct gpsd_unit    * const up = (struct gpsd_unit *)pp->unitptr;
	
	/* This is used for timeout handling. Nothing that needs
	 * sub-second precison appens here, so receive/connec/retry
	 * timeouts are simply handled by a count down, and then we
	 * decide what to do by the socket values.
	 *
	 * Note that the timer stays at zero here, unless some of the
	 * functions set it to another value.
	 */
	if (up->tickover)
		--up->tickover;

	if (0 == up->tickover) {
		if (-1 != pp->io.fd)
			gpsd_stop_socket(pp, up);
		else if (-1 != up->pfd.fd)
			gpsd_test_socket(pp, up);
		else if (NULL != s_gpsd_addr)
			gpsd_init_socket(pp, up);
	}
}

/* =====================================================================
 * static local helpers
 */

static void
process_version(
	struct peer * const peer ,
	json_ctx    * const jctx ,
	const l_fp  * const rtime)
{
	struct refclockproc * const pp = peer->procptr;
	struct gpsd_unit    * const up = (struct gpsd_unit *)pp->unitptr;

	int    len;
	char * buf;

	/* get protocol version number */
	errno = 0;
	up->proto_major = (uint16_t)json_object_lookup_int(
		jctx, 0, "proto_major");
	up->proto_minor = (uint16_t)json_object_lookup_int(
		jctx, 0, "proto_minor");
	if (0 == errno) {
		up->fl_ver = -1;
		printf("\n\nprotocol version = %u.%u\n\n\n",
		       up->proto_major, up->proto_minor);
	}
	/*TODO: validate protocol version! */
	
	/* request watch for our GPS device
	 * Reuse the input buffer, which is no longer needed in the
	 * current cycle. Also assume that we can write the watch
	 * request in one sweep into the socket; since we do not do
	 * output otherwise, this should always work.  (Unless the
	 * TCP/IP window size gets lower than the length of the
	 * request. We handle that whe it happens.)
	 */
	snprintf(up->buffer, sizeof(up->buffer),
		 s_logon, up->device);
	buf = up->buffer;
	len = strlen(buf);
	if (len != write(pp->io.fd, buf, len)) {
		/*TODO: log request error! */
		/*Note: if the server fails to read our request, the
		 * resulting data timeout will take care of the
		 * connection!
		 */
	}
}

/* ------------------------------------------------------------------ */

static void
process_tpv(
	struct peer * const peer ,
	json_ctx    * const jctx ,
	const l_fp  * const rtime)
{
	struct refclockproc * const pp = peer->procptr;
	struct gpsd_unit    * const up = (struct gpsd_unit *)pp->unitptr;

	const char * gps_time;
	uint8_t      gps_mode;
	
	errno = 0;
	gps_mode = (uint8_t)json_object_lookup_int(
		jctx, 0, "mode");
	gps_time = json_object_lookup_string(
		jctx, 0, "time");
	switch (errno) {
	case 0:
		save_ltc(pp, gps_time);
		if (convert_time(&up->tpv_stamp, gps_time)) {
			printf(" tpv, stamp='%s', recvt='%s' mode=%u\n",
			       gmprettydate(&up->tpv_stamp),
			       gmprettydate(&up->tpv_recvt),
			       gps_mode);
			
			up->tpv_recvt = *rtime;
			up->fl_tpv = -1;
		} else {
			refclock_report(peer, CEVNT_BADTIME);
		}
		break;
		
	default:
		refclock_report(peer, CEVNT_BADREPLY);
		break;
	}
}

/* ------------------------------------------------------------------ */

static void
process_pps(
	struct peer * const peer ,
	json_ctx    * const jctx ,
	const l_fp  * const rtime)
{
	struct refclockproc * const pp = peer->procptr;
	struct gpsd_unit    * const up = (struct gpsd_unit *)pp->unitptr;

	uint32_t secs;
	double   frac;
		
	errno = 0;
	secs = (uint32_t)json_object_lookup_int(
		jctx, 0, "real_sec");
	frac = json_object_lookup_float(
		jctx, 0, "real_musec");
	switch (errno) {
	case 0:
		frac *= 1.0e-6;
		M_DTOLFP(frac, up->pps_stamp.l_ui, up->pps_stamp.l_uf);
		up->pps_stamp.l_ui += secs;
		up->pps_stamp.l_ui += JAN_1970;
		printf(" pps, stamp='%s', recvt='%s'\n", 
		       gmprettydate(&up->pps_stamp),
		       gmprettydate(&up->pps_recvt));
		
		up->pps_recvt = *rtime;
		/* When we have a time pulse, clear the TPV flag: the
		 * PPS is only valid for the >NEXT< TPV value!
		 */
		up->fl_pps = -1;
		up->fl_tpv =  0;
		break;
		
	case ERANGE:
		refclock_report(peer, CEVNT_BADREPLY);
		break;
		
	default:
		refclock_report(peer, CEVNT_BADTIME);
		break;
	}
}

/* ------------------------------------------------------------------ */

static void
gpsd_parse(
	struct peer * const peer,
	const l_fp  * const rtime)
{
	struct refclockproc * const pp = peer->procptr;
	struct gpsd_unit    * const up = (struct gpsd_unit *)pp->unitptr;

	json_ctx     jctx;
	const char * clsid;

        DPRINTF(2, ("gpsd_parse: time %s '%s'\n",
                    ulfptoa(rtime, 6), up->buffer));

	/* See if we can ab anything potentially useful */
	if (!json_parse_record(&jctx, up->buffer))
		return;

	/* Now dispatch over the objects we know */
	clsid = json_object_lookup_string(&jctx, 0, "class");
	if (NULL == clsid)
		return;
	/*printf("--> class='%s'\n", clsid);*/
	if (!strcmp("VERSION", clsid))
		process_version(peer, &jctx, rtime);
	else if (!strcmp("TPV", clsid))
		process_tpv(peer, &jctx, rtime);
	else if (!strcmp("PPS", clsid))
		process_pps(peer, &jctx, rtime);
	else
		return; /* nothin we know about... */

	/* Bail out unless we have a pulse and a time stamp */
	if (!(up->fl_pps && up->fl_tpv))
		return;

	/* Check if the pulse and he time came close enough to be
	 * correlated. Ignore this pair if the difference is more than a
	 * second.
	 */
	L_SUB(&up->tpv_recvt, &up->pps_recvt);
	if (0 == up->tpv_recvt.l_ui) {
		/*TODO: set precision based on TDOP */
		peer->flags    |= FLAG_PPS;
		peer->precision = PPS_PRECISION;
		/*TODO: logging, fudge processing */
		refclock_process_offset(pp, up->tpv_stamp, up->pps_stamp,
					pp->fudgetime1);
	}

	/* mark pulse and time as consumed */
	up->fl_pps = 0;
	up->fl_tpv = 0;
}

/* ------------------------------------------------------------------ */

static void
gpsd_stop_socket(
	struct refclockproc * const pp,
	struct gpsd_unit    * const up)
{
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);
	pp->io.fd = -1;
	printf("\n\n\n CLOSED!\n\n\n");
	up->tickover = TICKOVER_MAX;
}

/* ------------------------------------------------------------------ */

static void
gpsd_init_socket(
	struct refclockproc * const pp,
	struct gpsd_unit    * const up)
{
	struct addrinfo     * ai;
	int rc;

	/* draw next address to try */
	if (NULL == up->addr)
		up->addr = s_gpsd_addr;
	ai = up->addr;
	up->addr = ai->ai_next;

	/* try to create a matching socket */
	up->pfd.fd = socket(ai->ai_family  ,
			    ai->ai_socktype,
			    ai->ai_protocol);
	if (-1 == up->pfd.fd)
		goto no_socket;
	
	/* make sure the socket is non-blocking */
	rc = fcntl(up->pfd.fd, F_SETFL, O_NONBLOCK, 1);
	if (-1 == rc)
		goto no_socket;
	
	/* start a non-blocking connect */
	rc = connect(up->pfd.fd    ,
		     ai->ai_addr   ,
		     ai->ai_addrlen);
	if (-1 == rc && errno != EINPROGRESS)
		goto no_socket;

	return;
  
  no_socket:
	if (-1 != up->pfd.fd)
		close(up->pfd.fd);
	up->pfd.fd = -1;
}

/* ------------------------------------------------------------------ */

static void
gpsd_test_socket(
	struct refclockproc * const pp,
	struct gpsd_unit    * const up)
{
	/* check if the socket becomes writeable */
	if (1 != poll(&up->pfd, 1, 0))
		return;

	/* next timeout is a full one... */
	up->tickover = TICKOVER_MAX;

	/* check for socket error */
	int       error = 0;
	socklen_t erlen = sizeof(error);
	getsockopt(up->pfd.fd, SOL_SOCKET, SO_ERROR, &error, &erlen);
	if (0 != error)
		goto no_socket;

	/* swap socket FDs, and make sure the clock was added */
	pp->io.fd = up->pfd.fd;
	up->pfd.fd = -1;
	if (0 == io_addclock(&pp->io))
		goto no_socket;
	up->tickover = TICKOVER_MAX;

	return;
	
  no_socket:
	if (-1 != up->pfd.fd)
		close(up->pfd.fd);
	up->pfd.fd = -1;
}

/* =====================================================================
 * helper stuff
 */
static BOOL
convert_time(
	l_fp       * fp      ,
	const char * gps_time)
{
	char     *ep, *sp;
	struct tm gd;
	double    frac;

	/* Use 'strptime' to take the brunt of the work, then use 'strtoul'
	 * to read in any fractional part.
	 */
	ep = strptime(gps_time, "%Y-%m-%dT%H:%M:%S", &gd);
	if (*ep == '.') {
		errno = 0;
		frac = strtoul((sp = ep + 1), &ep, 10);
		if (errno && ep != sp)
			return FALSE;
		for (/*NOP*/; sp != ep; ++sp)
			frac *= 0.1;
	} else {
		frac = 0.0;
	}
	if (ep[0] != 'Z' || ep[1] != '\0')
		return FALSE;

	/* now convert the whole thing into a 'l_fp' */
	/*TODO: refactor the calendar stuff into 'ntp_calendar.c' */
	M_DTOLFP(frac, fp->l_ui, fp->l_uf);
	fp->l_ui += (ntpcal_tm_to_rd(&gd) - DAY_NTP_STARTS) * SECSPERDAY;
	fp->l_ui +=  ntpcal_tm_to_daysec(&gd);

	return TRUE;
}

/*
 * -------------------------------------------------------------------
 * Save the last timecode string, making sure it's properly truncated
 * if necessary and NUL terminated in any case.
 */
static void
save_ltc(
	struct refclockproc * const pp,
	const char * const          tc)
{
	size_t len;

	len = (tc) ? strlen(tc) : 0;
	if (len >= sizeof(pp->a_lastcode))
		len = sizeof(pp->a_lastcode) - 1;
	pp->lencode = (u_short)len;
	memcpy(pp->a_lastcode, tc, len);
	pp->a_lastcode[len] = '\0';
}


/* =====================================================================
 * JSON parsing stuff
 */

static tok_ref
json_token_skip(
	const json_ctx * ctx,
	tok_ref          tid)
{
	int len;
	len = ctx->tok[tid].size;
	for (++tid; len; --len)
		if (tid < ctx->ntok)
			tid = json_token_skip(ctx, tid);
		else
			break;
	if (tid > ctx->ntok)
		tid = ctx->ntok;
	return tid;
}
	
/* ------------------------------------------------------------------ */

static int
json_object_lookup(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key)
{
	int len;

	if (tid >= ctx->ntok || ctx->tok[tid].type != JSMN_OBJECT)
		return INVALID_TOKEN;
	len = ctx->ntok - tid - 1;
	if (len > ctx->tok[tid].size)
		len = ctx->tok[tid].size;
	for (tid += 1; len > 1; len-=2) {
		if (ctx->tok[tid].type != JSMN_STRING)
			continue; /* hmmm... that's an error, strictly speaking */
		if (!strcmp(key, ctx->buf + ctx->tok[tid].start))
			return tid + 1;
		tid = json_token_skip(ctx, tid + 1);
	}
	return INVALID_TOKEN;
}

/* ------------------------------------------------------------------ */

static const char*
json_object_lookup_string(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key)
{
	tok_ref val_ref;
	val_ref = json_object_lookup(ctx, tid, key);
	if (INVALID_TOKEN == val_ref               ||
	    JSMN_STRING   != ctx->tok[val_ref].type )
		goto cvt_error;
	return ctx->buf + ctx->tok[val_ref].start;

  cvt_error:
	errno = EINVAL;
	return NULL;
}

/* ------------------------------------------------------------------ */

#ifdef HAVE_LONG_LONG
static long long
json_object_lookup_int(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key)
{
	long long ret;
	tok_ref   val_ref;
	char    * ep;

	val_ref = json_object_lookup(ctx, tid, key);
	if (INVALID_TOKEN  == val_ref               ||
	    JSMN_PRIMITIVE != ctx->tok[val_ref].type )
		goto cvt_error;
	ret = strtoll(ctx->buf + ctx->tok[val_ref].start, &ep, 10);
	if (*ep)
		goto cvt_error;
	return ret;

  cvt_error:
	errno = EINVAL;
	return 0;
}
#else
#endif

/* ------------------------------------------------------------------ */

static double
json_object_lookup_float(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key)
{
	double    ret;
	tok_ref   val_ref;
	char    * ep;

	val_ref = json_object_lookup(ctx, tid, key);
	if (INVALID_TOKEN  == val_ref               ||
	    JSMN_PRIMITIVE != ctx->tok[val_ref].type )
		goto cvt_error;
	ret = strtod(ctx->buf + ctx->tok[val_ref].start, &ep);
	if (*ep)
		goto cvt_error;
	return ret;

  cvt_error:
	errno = EINVAL;
	return 0.0;
}

/* ------------------------------------------------------------------ */

static BOOL
json_parse_record(
	json_ctx * ctx,
	char     * buf)
{
	jsmn_parser jsm;
	int         idx, rc;

	jsmn_init(&jsm);
	rc = jsmn_parse(&jsm, buf, ctx->tok, JSMN_MAXTOK);
	ctx->buf  = buf;
	ctx->ntok = jsm.toknext;

	/* Make all tokens NUL terminated by overwriting the
	 * terminator symbol
	 */
	for (idx = 0; idx < jsm.toknext; ++idx)
		if (ctx->tok[idx].end > ctx->tok[idx].start)
			ctx->buf[ctx->tok[idx].end] = '\0';

	if (JSMN_ERROR_PART  != rc &&
	    JSMN_ERROR_NOMEM != rc &&
	    JSMN_SUCCESS     != rc  )
		return FALSE; /* not parseable - bail out */

	if (0 >= jsm.toknext || JSMN_OBJECT != ctx->tok[0].type)
		return FALSE; /* not object or no data!?! */

	return TRUE;
}

#else
NONEMPTY_TRANSLATION_UNIT
#endif /* REFCLOCK && CLOCK_GPSDJSON */