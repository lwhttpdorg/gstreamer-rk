#!/usr/bin/python3

import time
import os
import sys
import gitlab

CERBERO_PROJECT = 'gstreamer/cerbero'


class Status:
    FAILED = 'failed'
    MANUAL = 'manual'
    CANCELED = 'canceled'
    SUCCESS = 'success'
    SKIPPED = 'skipped'
    CREATED = 'created'

    @classmethod
    def is_finished(cls, state):
        return state in [
            cls.FAILED,
            cls.MANUAL,
            cls.CANCELED,
            cls.SUCCESS,
            cls.SKIPPED,
        ]


def fprint(msg):
    print(msg, end="")
    sys.stdout.flush()


def find_existing_pipeline(cerbero, cerbero_branch, variables):
    fprint(
        f"-> Looking for existing pipelines in {cerbero.path_with_namespace} for branch {cerbero_branch} with variables {variables}...\n")

    try:
        pipelines = cerbero.pipelines.list(status='running',
                                           ref=cerbero_branch,
                                           # token=os.environ['CI_JOB_TOKEN'],
                                           iterator=True)

    except Exception as e:
        fprint(f'\tError: {e}\n')
        return None

    for p in pipelines:
        pipeline_env = {}
        for var in p.variables.list(token=os.environ['CI_JOB_TOKEN'], iterator=True):
            v = var.asdict()
            if v['variable_type'] == 'env_var':
                pipeline_env[v['key']] = v['value']

        print('Pipeline', p, 'with variables', pipeline_env, '\n')

        if pipeline_env == variables:
            return p

    return None


if __name__ == "__main__":
    server = os.environ['CI_SERVER_URL']
    gl = gitlab.Gitlab(server,
                       private_token=os.environ.get('GITLAB_API_TOKEN'),
                       job_token=os.environ.get('CI_JOB_TOKEN'))

    def get_matching_user_project(project, branch):
        cerbero = gl.projects.get(project)
        # Search for matching branches, return only if the branch name matches
        # exactly
        for b in cerbero.branches.list(search=cerbero_branch, iterator=True):
            if branch == b.name:
                return cerbero
        return None

    cerbero = None
    # Only look for user namespace cerbero branch when running in a merge request
    if "CI_MERGE_REQUEST_SOURCE_BRANCH_NAME" in os.environ:
        print("GStreamer monorepo merge request")
        cerbero_branch = os.environ["CI_MERGE_REQUEST_SOURCE_BRANCH_NAME"]
        user_project_path = os.environ["CI_MERGE_REQUEST_SOURCE_PROJECT_PATH"]
        user_ns = os.path.dirname(user_project_path)
        cerbero_name = f'{user_ns}/cerbero'
        # We do not want to run on (often out of date) user upstream branch
        if os.environ["CI_MERGE_REQUEST_SOURCE_BRANCH_NAME"] != os.environ["GST_UPSTREAM_BRANCH"]:
            try:
                cerbero = get_matching_user_project(cerbero_name, cerbero_branch)
            except gitlab.exceptions.GitlabGetError as e:
                print("No matching user project found: " + str(e))
                pass

    if cerbero is None:
        print("Using gstreamer org namespace")
        cerbero_name = CERBERO_PROJECT
        cerbero_branch = os.environ["GST_UPSTREAM_BRANCH"]
        cerbero = gl.projects.get(cerbero_name)

    # CI_PROJECT_URL is not necessarily the project where the branch we need to
    # build resides, for instance merge request pipelines can be run on
    # 'gstreamer' namespace. Fetch the branch name in the same way, just in
    # case it breaks in the future.
    if 'CI_MERGE_REQUEST_SOURCE_PROJECT_URL' in os.environ:
        project_path = os.environ['CI_MERGE_REQUEST_SOURCE_PROJECT_PATH']
        project_branch = os.environ['CI_MERGE_REQUEST_SOURCE_BRANCH_NAME']
    else:
        project_path = os.environ['CI_PROJECT_PATH']
        project_branch = os.environ['CI_COMMIT_REF_NAME']

    commit_sha = os.environ['CI_COMMIT_SHORT_SHA']

    variables = {
        "CI_GSTREAMER_PATH": project_path,
        "CI_GSTREAMER_REF_NAME": project_branch,
        # This tells cerbero CI that this is a pipeline started via the
        # trigger API, which means it can use a deps cache instead of
        # building from scratch.
        "CI_GSTREAMER_TRIGGERED": "true",
        # Pass the commit sha of the parent pipeline, so that if we later
        # try to find a matching sub-pipeline we don't accidentally use one
        # that was an older parent project revision but is still running.
        "CI_GSTREAMER_PARENT_PROJECT_COMMIT_SHORT_SHA": commit_sha,
    }

    meson_commit = os.environ.get('MESON_COMMIT')
    if meson_commit:
        # Propagate the Meson commit to cerbero pipeline and make sure it's not
        # using deps cache.
        variables['MESON_COMMIT'] = meson_commit
        del variables['CI_GSTREAMER_TRIGGERED']

    pipe = find_existing_pipeline(cerbero, cerbero_branch, variables)

    if not pipe:
        fprint(f"-> Triggering on branch {cerbero_branch} in {cerbero_name}\n")
        try:
            pipe = cerbero.trigger_pipeline(
                token=os.environ['CI_JOB_TOKEN'],
                ref=cerbero_branch,
                variables=variables,
            )
        except gitlab.exceptions.GitlabCreateError as e:
            if e.response_code == 400:
                exit('''

                Could not start cerbero sub-pipeline due to insufficient permissions.

                This is not a problem and is expected if you are not a GStreamer
                developer with merge permission in the cerbero project.

                When your Merge Request is assigned to Marge (our merge bot), it
                will trigger the cerbero sub-pipeline with the correct permissions.
                ''')
            else:
                exit(f'Could not create cerbero sub-pipeline. Error: {e}')
    else:
        fprint('Found existing running cerbero sub-pipeline.\n')

    fprint(f'Cerbero pipeline running at {pipe.web_url} ')
    while True:
        time.sleep(15)
        pipe.refresh()
        if Status.is_finished(pipe.status):
            fprint(f": {pipe.status}\n")
            sys.exit(0 if pipe.status == Status.SUCCESS else 1)
        else:
            fprint(".")
