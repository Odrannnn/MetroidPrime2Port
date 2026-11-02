# ds38: the guest-record helpers the plain fsm2 run (darksamus_fsm2.py) leaves half taken, forced
# on the coverage build. Run with tools/cov/force_ret.py as the gdb side:
#   COV_GDB_SCRIPT=tools/cov/force_ret.py BUILD=$PWD/build-port-cov MP2_INPUT=LAYER2@400 \
#     tools/cov/force.sh tools/cov/specs/darksamus_guest_force.py <out> "1baa96c2,10_cliff_rooftop" \
#     --con tools/cov/specs/darksamus_fsm2.con --fast --frames 4000
# FORCE_RET (written at the entry, put back at the return):
#   48C8 DeleteGuestActorIfExpired  every 2nd call: 0xF7C = the elapsed time - 1 (0x000048F4 not
#                                   taken; the delete runs only when 0xF78 holds a uid)
#   69C0 DeleteGuestUids            every call: count 0x930 = 2, slot 0 kInvalidUniqueId
#                                   (0x000069F8 taken), slot 1 a stale uid (the own uid's
#                                   generation + 1: DeleteObjectRequest finds nothing)
CLASS = 'CDarkSamus'
SPEC = [
    ('48C8 DeleteGuestActorIfExpired', 'CDarkSamus::DeleteGuestActorIfExpired(',
     ['$this->mGuestF78.value', '$this->mGuestF7C', '$this->mElapsedTime'], 'calls max=200'),
    ('69C0 DeleteGuestUids', 'CDarkSamus::DeleteGuestUids(', ['$this->mGuest930'], 'calls max=200'),
]
FORCE_RET = [
    ('48C8 expired (forced)', 'CDarkSamus::DeleteGuestActorIfExpired(', 2,
     [(1, [('$this->mGuestF7C', '$this->mElapsedTime - 1.0')])],
     'the timestamp older than the elapsed time (0x000048F4 not taken)'),
    ('69C0 two slots (forced)', 'CDarkSamus::DeleteGuestUids(', 1,
     [(0, [('$this->mGuest930', '2'), ('$this->mGuest934[0]', '0xffff'),
           ('$this->mGuest934[1]', '((*(unsigned short*)&$this->mUniqueId) + 0x800) & 0xffff')])],
     'an invalid slot (0x000069F8) and a stale uid deleted (0x00006A0C)'),
]
