#!/bin/sh

test_description='git checkout --patch'
. ./test-lib.sh

test_expect_success 'setup' '
	mkdir dir &&
	echo parent > dir/foo &&
	echo dummy > dir/bar &&
	git add dir &&
	git commit -m initial &&
	test_tick &&
	test_commit second dir/foo head &&
	echo work > dir/foo &&
	echo bar_index > dir/bar &&
	git add dir/bar &&
	echo bar_work > dir/bar
'

# note: bar sorts before foo, so the first 'n' is always to skip 'bar'

test_expect_success 'saying "n" does nothing' '
	(echo n; echo n) | git checkout -p &&
	test "$(cat dir/foo)" = work
'

test_expect_success 'git checkout -p' '

	(echo n; echo y) | git checkout -p &&
	test "$(cat dir/foo)" = head &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = bar_work
'

test_expect_success 'git checkout -p with staged changes' '
	echo index > dir/foo &&
	git add dir/foo &&
	echo work > dir/foo &&
	(echo n; echo y) | git checkout -p &&
	test "$(git show :dir/foo)" = index &&
	test "$(cat dir/foo)" = index &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = bar_work
'

test_expect_success 'git checkout -p HEAD with NO staged changes: abort' '
	git reset -q -- dir/foo &&
	echo work > dir/foo &&
	(echo n; echo y; echo n) | git checkout -p HEAD &&
	test "$(git show :dir/foo)" = head &&
	test "$(cat dir/foo)" = work &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = bar_work
'

test_expect_success 'git checkout -p HEAD with NO staged changes: apply' '
	(echo n; echo y; echo y) | git checkout -p HEAD &&
	test "$(git show :dir/foo)" = head &&
	test "$(cat dir/foo)" = head &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = bar_work
'

test_expect_success 'git checkout -p HEAD with change already staged' '
	echo index > dir/foo &&
	git add dir/foo &&
	# the third n is to get out in case it mistakenly does not apply
	(echo n; echo y; echo n) | git checkout -p HEAD &&
	test "$(git show :dir/foo)" = head &&
	test "$(cat dir/foo)" = head &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = bar_work
'

test_expect_success 'git checkout -p HEAD^' '
	# the third n is to get out in case it mistakenly does not apply
	(echo n; echo y; echo n) | git checkout -p HEAD^ &&
	test "$(git show :dir/foo)" = parent &&
	test "$(cat dir/foo)" = parent &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = bar_work
'

test_done
